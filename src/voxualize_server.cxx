#include <fstream>
#include <string>
#include <cstdlib>
#include <stdio.h>
#include <stdlib.h>
#include <iostream>
#include <chrono>
#include <thread>
#include <vector>
#include "zfp.h"
#include <mutex>
#include<thread>

// ffmpeg
extern "C" {
  #include <libavcodec/avcodec.h>
  #include <libavutil/opt.h>
  #include <libavutil/imgutils.h>
  #include <libswscale/swscale.h>
}

#include <vtkCamera.h>
#include <vtkColorTransferFunction.h>
#include <vtkDataArray.h>
#include <vtkDataSet.h>
#include <vtkFixedPointVolumeRayCastMapper.h>
#include <vtkImageData.h>
#include <vtkImageImport.h>
#include <vtkImageReader.h>
#include <vtkImageShiftScale.h>
#include <vtkNamedColors.h>
#include <vtkPiecewiseFunction.h>
#include <vtkPointData.h>
#include <vtkRenderer.h>
#include <vtkRenderWindow.h>
#include <vtkRenderWindowInteractor.h>
#include <vtkEGLRenderWindow.h>
#include <vtkSmartPointer.h>
#include <vtkSmartVolumeMapper.h>
#include <vtkStructuredPointsReader.h>
#include <vtkVolume.h>
#include <vtkVolumeProperty.h>
#include <vtkXMLImageDataReader.h>
#include <vtkFloatArray.h>
#include <vtkImageActor.h>
#include <vtkInteractorStyleImage.h>
#include <vtkWindowToImageFilter.h>
#include <vtkPNGWriter.h>

#include <grpcpp/grpcpp.h>
#include <grpcpp/health_check_service_interface.h>
#include <grpcpp/ext/proto_server_reflection_plugin.h>

#include <grpc/grpc.h>
#include <grpcpp/server.h>
#include <grpcpp/server_builder.h>
#include <grpcpp/server_context.h>
#include <google/protobuf/repeated_field.h>

#include "boost/filesystem.hpp"   // includes all needed Boost.Filesystem declarations
#include "boost/filesystem/path.hpp"
#include <iostream>
namespace fs = boost::filesystem;

#include "voxualize.grpc.pb.h"
#include "data_cube.h"

using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::ServerWriter;
using grpc::ServerReader;
using grpc::ServerReaderWriter;
using grpc::Status;
using voxualize::FileDetails;
using voxualize::Greeter;
using voxualize::FilesRequest;
using voxualize::FilesList;
using voxualize::CameraInfo;
using voxualize::HQRender;
using voxualize::ROILOD;
using voxualize::Empty;

using namespace std;

class GreeterServiceImpl final : public Greeter::Service {

  const int bytes_per_write = 64*64*64; // Performance tests to be run on this.

  DataCube dataCube;
  vtkSmartPointer<vtkFloatArray> floatArray;
  vtkSmartPointer<vtkImageImport> imageImport;
  vtkSmartPointer<vtkSmartVolumeMapper> volumeMapper;
  vtkSmartPointer<vtkNamedColors> colors;
  vtkSmartPointer<vtkRenderer> ren1;
  vtkSmartPointer<vtkEGLRenderWindow> renWin;
  vtkSmartPointer<vtkPiecewiseFunction> opacityTransferFunction;
  vtkSmartPointer<vtkColorTransferFunction> colorTransferFunction;
  vtkSmartPointer<vtkVolumeProperty> volumeProperty;
  vtkSmartPointer<vtkVolume> volume;
  vtkSmartPointer<vtkRenderWindowInteractor> iren;

  vtkSmartPointer<vtkImageData> imageData;

  AVPacket encodedFramePkt;

  // Vectors used for compression.
  vector<char> compression_buffer;
  size_t compression_size;
  vector<float> array;

  long num_bytes;
  unsigned char * pixelData;
  int number_of_bytes;
  bool is_egl_started;

  int window_width;
  int window_height;

  unsigned char * pointer;

  int oldSB;

  std::mutex mtx;
  thread start_render_thread;

  // Service to return the data of a specified file, and starts the EGL render on the server.
  Status ChooseFile(ServerContext *context, const FileDetails *request,
                  ServerWriter<ROILOD> *writer) override {
    mtx.lock();
    cout << "ChooseFile rpc" << endl;
    std::string file_name(request->file_name());
    switch (request->s_method()) {
      case FileDetails::Max :
        dataCube.s_method = "Max";
        break;
      case FileDetails::Mean :
        dataCube.s_method = "Mean";
        break;
      default :
        dataCube.s_method = "Max";
        break;
    }
    dataCube.createCube(file_name);
    float cropping_dims[3];
    cropping_dims[0] = dataCube.dimx; cropping_dims[1] = dataCube.dimy; cropping_dims[2] = dataCube.dimz;
    cout << "request->target_size_lod_bytes(): " <<  request->target_size_lod_bytes() << endl;
    if (request->target_size_lod_bytes() == 0)
      dataCube.generateLODModel(10);
    else
      dataCube.generateLODModel(request->target_size_lod_bytes());
    
    start_render_thread = std::thread(&GreeterServiceImpl::createEGLRenderOnServer, this); // would be nice if this is done with a new thread and is thread-safe.
    streamLODModel(writer);
    mtx.unlock();
    return Status::OK;
  }

  Status ListFiles(ServerContext *context, const FilesRequest *request,
                  FilesList *reply) override {
    mtx.lock();
    // Retrieve a list of files to view
    std::cout<< "ListFiles rpc" << std::endl;
    fs::path dir_path("../../../Data/");
    createFilesListResponse( dir_path, reply);
    is_egl_started = false;
    mtx.unlock();
    return Status::OK;
  }

  Status GetHQRender(ServerContext *context, const CameraInfo *request,
                        ServerWriter<HQRender> *writer) override {
    if (is_egl_started == false) {
      start_render_thread.join(); // This RPC wont execute untill the egl render is set up.
      is_egl_started = true;
    }
    
    mtx.lock();
    cout << "GetHQRender rpc" << endl;
    // if (!is_egl_started){
    //   createEGLRenderOnServer();
    //   is_egl_started = true;
    // }
    // Get pointer to data.
    pixelData = updateCameraAndGetData(request); //and save screenshot....for now.

    float * pixelDataFloats = reinterpret_cast<float *>(pixelData);
    // for ( int i=180000; i<180100; i++){
    //   cout << *(pixelDataFloats+1) << ' ';
    // }

    // Encode with NVENC / FFMPEG ? Again return a pointer to encoded data
    AVPacket * pkt = encodePixelData(pixelData, request);
    //reply->set_size_in_bytes(pkt->size);
    // reply->window_width(1836);
    // reply->window_height(500);
    // for ( int i=180000; i<180100; i++){
    //   cout << *(pixelDataFloats+1) << ' ';
    // }
    //number_of_bytes = imageData->GetNumberOfPoints()*4;
    //reply->set_size_in_bytes(imageData->GetNumberOfPoints()*4);
    streamHQRender(writer);
    mtx.unlock();
    return Status::OK;
  }

  Status GetNewROILOD(ServerContext *context, const CameraInfo *request,
                            ServerWriter<ROILOD> *writer) override {
    mtx.lock();
    switch (request->s_method()) {
      case CameraInfo::Max :
        dataCube.s_method = "Max";
        break;
      case CameraInfo::Mean :
        dataCube.s_method = "Mean";
        break;
      default :
        dataCube.s_method = "Max";
        break;
    }
    // Given the cropping planes info, compute new LOD model.
    cout << "GetNewROILOD rpc" << endl;
    const google::protobuf::RepeatedField<float> cplanes = request->cropping_planes();
    float cropping_dims[6];
    cropping_dims[0] = cplanes.Get(0); cropping_dims[1] = cplanes.Get(1); cropping_dims[2] = cplanes.Get(2);
    cropping_dims[3] = cplanes.Get(3); cropping_dims[4] = cplanes.Get(4); cropping_dims[5] = cplanes.Get(5);

    cout << "Request for new ROI, memsize or both detected." << endl;
    dataCube.generateLODModelNew(request->target_size_lod_bytes(), &cropping_dims[0]);

    // reply->set_true_size_lod_bytes(dataCube.LOD_num_bytes);
    // reply->add_dimensions_lod(dataCube.new_dim_x);
    // reply->add_dimensions_lod(dataCube.new_dim_y);
    // reply->add_dimensions_lod(dataCube.new_dim_z);
    streamLODModel(writer);
    cout << "Finished GetNewROILOD rpc" << endl;
    mtx.unlock();
    return Status::OK;
  }

  Status Reset(ServerContext *context, const CameraInfo *request,
                            Empty *reply) override {
    dataCube.generateLODModel(request->target_size_lod_bytes());
    return Status::OK;
  }

  void createEGLRenderOnServer(){
    cout << "Creating EGL render on server." << endl;
    floatArray = vtkSmartPointer<vtkFloatArray>::New();
    floatArray->SetName("Float Array");
    floatArray->SetArray(dataCube.floatArray, dataCube.num_pixels, 1);

    // Create vtkImageImport object in order to use an array as input data to the volume mapper.
    imageImport = vtkSmartPointer<vtkImageImport>::New();
    imageImport->SetDataScalarTypeToFloat();
    imageImport->SetWholeExtent(0, dataCube.dimx - 1, 0, dataCube.dimy - 1, 0, dataCube.dimz - 1);
    imageImport->SetDataExtentToWholeExtent();
    imageImport->SetImportVoidPointer(floatArray->GetVoidPointer(0));
    imageImport->SetNumberOfScalarComponents(1);
    //imageImport->SetDataSpacing(1.0, (double)(dataCube.dimx) / dataCube.dimy, (double)(dataCube.dimx) / dataCube.dimz);
    imageImport->SetDataSpacing(1.0, 1.0, 1.0);
    imageImport->Update();

    // The mapper / ray cast function know how to render the data
    volumeMapper = vtkSmartPointer<vtkSmartVolumeMapper>::New();
    volumeMapper->SetBlendModeToComposite(); // composite first
    volumeMapper->SetInputConnection(imageImport->GetOutputPort());
    volumeMapper->CroppingOn();

    // Create the standard renderer, render window
    // and interactor
    colors = vtkSmartPointer<vtkNamedColors>::New();

    ren1 = vtkSmartPointer<vtkRenderer>::New();

    renWin = vtkSmartPointer<vtkEGLRenderWindow>::New();
    renWin->Initialize();
    renWin->AddRenderer(ren1);

    // Create transfer mapping scalar value to opacity
    // Data values for ds9.arr 540x450x201 are in range [-0.139794;0.153026]
    opacityTransferFunction = vtkSmartPointer<vtkPiecewiseFunction>::New();
    opacityTransferFunction->AddPoint(-0.0, 0.0);
    opacityTransferFunction->AddPoint(0.16, 1.0);

    // Create transfer mapping scalar value to color
    colorTransferFunction = vtkSmartPointer<vtkColorTransferFunction>::New();
    colorTransferFunction->AddRGBPoint(-0.0, 0.0, 0.0, 0.0);
    colorTransferFunction->AddRGBPoint(0.16, 1.0, 1.0, 1.0);

    // The property describes how the data will look
    volumeProperty = vtkSmartPointer<vtkVolumeProperty>::New();
    volumeProperty->SetColor(colorTransferFunction);
    volumeProperty->SetScalarOpacity(opacityTransferFunction);
    volumeProperty->SetInterpolationTypeToNearest();

    // The volume holds the mapper and the property and
    // can be used to position/orient the volume
    volume = vtkSmartPointer<vtkVolume>::New();
    volume->SetMapper(volumeMapper);
    volume->SetProperty(volumeProperty);

    ren1->SetBackground(220, 185, 152);
    ren1->AddVolume(volume);
    ren1->GetActiveCamera()->Azimuth(45);
    ren1->GetActiveCamera()->Elevation(30);
    ren1->ResetCameraClippingRange();
    ren1->ResetCamera();
    //renWin->SwapBuffersOff();
    cout << "Is current? : " << renWin->IsCurrent() << endl;
    renWin->SetSize(1836, 500);

    //renWin->Render();
    //renWin->Frame();
    //renWin->WaitForCompletion();
    //captureScreenShotOfCurrentEGLRender();

    double * position = ren1->GetActiveCamera()->GetFocalPoint();
    cout << position[0] << ' ' << position[1] << ' ' << position[2] << endl;
    cout << "Finished setting up EGL render on server." << endl;
    return;
  }

  // Get's the pixel data (pointer to it) from the current render on the backend.
  // Also uses information from the request to update the cameras position, get resolution, etc.
  unsigned char * updateCameraAndGetData(const CameraInfo *request){

    // Get variables
    const google::protobuf::RepeatedField<float> position = request->position();
    const google::protobuf::RepeatedField<float> focal_point = request->focal_point();
    const google::protobuf::RepeatedField<float> view_up = request->view_up();
    const google::protobuf::RepeatedField<float> rgb = request->rgba();
    const google::protobuf::RepeatedField<float> cplanes = request->cropping_planes();
    const float alpha = request->alpha();
    const double distance = request->distance();
    window_width = request->window_width();
    window_height = request->window_height();
    
    renWin -> Finalize();
    renWin-> Initialize();
    renWin->SetSize(window_width, window_height);
    // Set variables.
    //ren1->ResetCamera();
    ren1->GetActiveCamera()->SetPosition(position.Get(0)*dataCube.x_scale_factor,position.Get(1)*dataCube.y_scale_factor,position.Get(2)*dataCube.z_scale_factor);
    ren1->GetActiveCamera()->SetViewUp(view_up.Get(0),view_up.Get(1),view_up.Get(2));
    ren1->GetActiveCamera()->SetFocalPoint(focal_point.Get(0)*dataCube.x_scale_factor,focal_point.Get(1)*dataCube.y_scale_factor,focal_point.Get(2)*dataCube.z_scale_factor);
    //ren1->GetActiveCamera()->SetDistance(distance);
    colorTransferFunction->RemoveAllPoints();
    colorTransferFunction->AddRGBPoint(-0.0, 0.0, 0.0, 0.0);
    colorTransferFunction->AddRGBPoint(0.0, rgb.Get(0)/255, rgb.Get(1)/255, rgb.Get(2)/255);
    colorTransferFunction->AddRGBPoint(0.16, 1.0, 1.0, 1.0);


    cout << "Trying to set cropping planes" << endl;
    cout << "Requested cropping planes: " << cplanes.Get(0) << ' ' << cplanes.Get(1) << ' ' << cplanes.Get(2) << ' ' << 
                                            cplanes.Get(3) << ' ' << cplanes.Get(4) << ' ' << cplanes.Get(5)<< endl;
    cout << "Cropping planes set on backend: " << cplanes.Get(0)*dataCube.x_scale_factor << ' ' << cplanes.Get(1)*dataCube.x_scale_factor 
                                          << ' ' << cplanes.Get(2)*dataCube.y_scale_factor << ' ' << cplanes.Get(3)*dataCube.y_scale_factor 
                                          << ' ' << cplanes.Get(4)*dataCube.z_scale_factor << ' ' << cplanes.Get(5)*dataCube.z_scale_factor << endl;

   volumeMapper->SetCroppingRegionPlanes(cplanes.Get(0)*dataCube.x_scale_factor, cplanes.Get(1)*dataCube.x_scale_factor, 
                                        cplanes.Get(2)*dataCube.y_scale_factor, cplanes.Get(3)*dataCube.y_scale_factor, 
                                        cplanes.Get(4)*dataCube.z_scale_factor, cplanes.Get(5)*dataCube.z_scale_factor);
    //cout << "Sleep 1..." << endl;
    //sleep(5);
    //renWin->SwapBuffersOff();
    renWin->AddRenderer(ren1);
    cout << "Is current? : " << renWin->IsCurrent() << endl;
    renWin->Render();
    //sleep(5);
    renWin->Frame();
    //cout << "Sleep 3..." << endl;
    //sleep(5);
    renWin->WaitForCompletion();
    //renWin->CopyResultFrame();
    //cout << "Done!" << endl;
    cout << "Is current? : " << renWin->IsCurrent() << endl;

    oldSB = renWin->GetSwapBuffers();
    renWin->SwapBuffersOff();

    // //restore swapping state
    // renWin->SetSwapBuffers(oldSB);

    captureScreenShotOfCurrentEGLRender();

    vtkSmartPointer<vtkWindowToImageFilter> windowToImageFilter = 
      vtkSmartPointer<vtkWindowToImageFilter>::New();
    windowToImageFilter->SetInput(renWin);
    windowToImageFilter->SetInputBufferTypeToRGB(); //also record the alpha (transparency) channel
    windowToImageFilter->ReadFrontBufferOff(); // read from the back buffer
    windowToImageFilter->Update();

    imageData = windowToImageFilter->GetOutput();

    //cout << imageData->GetNumberOfCells() << endl;
    //cout << imageData->GetNumberOfPoints() << endl;

    int* dims = imageData->GetDimensions();
    std::cout << "Dims: " << " x: " << dims[0] << " y: " << dims[1] << " z: " << dims[2] << std::endl;
    //imageData -> PrintSelf(cout, vtkIndent(2)) ;
    pointer = static_cast<unsigned char *>(imageData->GetScalarPointer(0,0,0));
    //pointer = reinterpret_cast<unsigned char *>(renWin->GetPixelData(0,599,0, 599, 0, 0));
    // restore swapping state
    for ( int i=0; i<100; i++){
      cout << +*(pointer+i) << ' ';
    }
    renWin->SetSwapBuffers(oldSB);
    //renWin->Initialize();
    //renWin->AddRenderer(ren1);
    return pointer;
  }
  
  // Encode the data. For now using ffmpeg. Hopefully in future done using GPU acceleration.
  AVPacket * encodePixelData(unsigned char *pixelData, const CameraInfo *request){
    cout << "Attempting to encode" << endl;
    const AVCodec *codec;
    AVCodecContext *c= NULL;
    int i, ret, x, y, got_output;
    const char *codec_name = "libx264";
    AVFrame *frame;
    AVPacket *pkt = &encodedFramePkt;

    codec = avcodec_find_encoder_by_name(codec_name);

    if (!codec) {
      fprintf(stderr, "Codec '%s' not found\n", codec_name);
      exit(1);
    }

    c = avcodec_alloc_context3(codec);
    if (!c) {
      fprintf(stderr, "Could not allocate video codec context\n");
      exit(1);
    }

    /* put sample parameters */
    c->bit_rate = 400000;
    /* resolution must be a multiple of two */
    c->width = window_width;
    c->height = window_height;
    /* frames per second */
    c->time_base = (AVRational){1, 1};
    //c->framerate = (AVRational){1, 1};

    c->gop_size = 0; //Intra only - meaning the pictures should be constructed only from information
                      // within that picture, and not any other pictures.
    c->max_b_frames = 0;

    c->pix_fmt = AV_PIX_FMT_YUVJ420P;
    c->profile = FF_PROFILE_H264_CONSTRAINED_BASELINE;

    if (codec->id == AV_CODEC_ID_H264)
      av_opt_set(c->priv_data, "profile", "baseline", 0);

    ret = avcodec_open2(c, codec, NULL);
    if (ret < 0) {
      std::cout<< "Could not open codec: %s\n" << std::endl;
      exit(1);
    }

    frame = av_frame_alloc();
      if (!frame) {
          fprintf(stderr, "Could not allocate video frame\n");
          exit(1);
      }

    frame->format = c->pix_fmt;
    frame->width  = c->width;
    frame->height = c->height;

    /* the image can be allocated by any means and av_image_alloc() is
     * just the most convenient way if av_malloc() is to be used */
    ret = av_image_alloc(frame->data, frame->linesize, c->width, c->height,
                         c->pix_fmt, 32);
    if (ret < 0) {
        fprintf(stderr, "Could not allocate raw picture buffer\n");
        exit(6);
    }

    uint8_t *rgba32Data = reinterpret_cast<uint8_t*>(pixelData);
    
    SwsContext * ctx = sws_getContext(c->width, c->height,
                                      AV_PIX_FMT_RGB24, c->width, c->height,
                                      AV_PIX_FMT_YUVJ420P, 0, 0, 0, 0);

    //ctx->color_range = AVCOL_RANGE_JPEG;

    av_init_packet(pkt);
    pkt->data = NULL;    // packet data will be allocated by the encoder
    pkt->size = 0;

    fflush(stdout);
 
    uint8_t * inData[1] = { rgba32Data }; // RGBA32 have one plane

    int inLinesize[1] = { 3*c->width }; // RGBA stride

    sws_scale(ctx, inData, inLinesize, 0, c->height, frame->data, frame->linesize);
    
    frame->pts = i;

    /* encode the image */
    ret = avcodec_encode_video2(c, pkt, frame, &got_output);
    if (ret < 0) {
        fprintf(stderr, "Error encoding frame\n");
        exit(7);
    }

    FILE *f;
    const char *filename = "outfile.mpg";
    uint8_t endcode[] = { 0, 0, 1, 0xb7 };
    f = fopen(filename, "wb");
    i = 0;
    if (!f) {
      fprintf(stderr, "Could not open %s\n", filename);
      exit(4);
    }
    if (got_output) {
      printf("Write frame %3d (size=%5d)\n", i, pkt->size);
      fwrite(pkt->data, 1, pkt->size, f);
      //av_free_packet(pkt);
    }

    for (got_output = 1; got_output; i++) {
      fflush(stdout);
      
      ret = avcodec_encode_video2(c, pkt, NULL, &got_output);
      if (ret < 0) {
        fprintf(stderr, "Error encoding frame\n");
        exit(8);
      }
      
      if (got_output) {
        printf("Write frame %3d (size=%5d)\n", i, pkt->size);
        
        fwrite(pkt->data, 1, pkt->size, f);
        return pkt;
        //av_free_packet(pkt);
      }
    }
    /* add sequence end code to have a real mpeg file */
    fwrite(endcode, 1, sizeof(endcode), f);
    fclose(f);

    avcodec_close(c);
    av_free(c);
    av_freep(&frame->data[0]);
    av_frame_free(&frame);

    return &encodedFramePkt;
  }
  
  void captureScreenShotOfCurrentEGLRender(){
    vtkSmartPointer<vtkWindowToImageFilter> windowToImageFilter = 
      vtkSmartPointer<vtkWindowToImageFilter>::New();
    windowToImageFilter->SetInput(renWin);
    windowToImageFilter->SetInputBufferTypeToRGB(); //also record the alpha (transparency) channel
    windowToImageFilter->ReadFrontBufferOff(); // read from the back buffer
    windowToImageFilter->Update();
    
    vtkSmartPointer<vtkPNGWriter> writer1 = 
      vtkSmartPointer<vtkPNGWriter>::New();
    writer1->SetFileName("screenshot1.png");
    writer1->SetInputConnection(windowToImageFilter->GetOutputPort());
    writer1->Write();
  }

  // Function which receives a path to a directory and a pointer to the FilesList reply.
  // It must add all the file names and their sizes to the response.
  void createFilesListResponse(const fs::path & dir_path, FilesList *reply){
    fs::directory_iterator end_itr; // default construction yields past-the-end
    for ( fs::directory_iterator itr( dir_path ); itr != end_itr; ++itr ){
      if ( fs::is_directory(itr->status()) )
        createFilesListResponse(itr->path(), reply); // Recursive call for subdirectories
      else{
        if (itr->path().leaf().string() != "File_Information.txt"){
          FilesList::File* file = reply->add_files();
          file->set_file_name(itr->path().leaf().string());
          file->set_file_size((float)fs::file_size(itr->path())/1000000);
        }
      }
    }
  }

  void streamLODModel(ServerWriter<ROILOD> *writer){
    //char * bytes = dataCube.getBytePointerLODModel();
    //Do zfp compression.
    // cout << "Zfp compressing LOD model..." << endl;
    // int status = Compress(dataCube.LODFloatArray,  &compression_buffer, &compression_size, 
    //                       dataCube.new_dim_x, dataCube.new_dim_y, dataCube.new_dim_z, 11);
    // cout << "Finished compressing." << endl;
    // char * bytes = compression_buffer.data();
    // ROILOD d;
    // d.set_total_lod_bytes(dataCube.LOD_num_bytes);
    // d.add_dimensions_lod(dataCube.new_dim_x);
    // d.add_dimensions_lod(dataCube.new_dim_y);
    // d.add_dimensions_lod(dataCube.new_dim_z);
    // for (int i = 0; i < compression_size; i += bytes_per_write){
    //   if ( compression_size - i < bytes_per_write){
    //     d.set_bytes(bytes, compression_size - i);
    //     d.set_num_bytes(compression_size - i);
    //   } else {
    //     d.set_bytes(bytes, bytes_per_write);
    //     d.set_num_bytes(bytes_per_write);
    //   }
    //   writer->Write(d);
    //   bytes += bytes_per_write; // Update the pointer.
    // }

    char * bytes = dataCube.getBytePointerLODModel();
    ROILOD d;
    d.set_total_lod_bytes(dataCube.LOD_num_bytes);
    d.add_dimensions_lod(dataCube.new_dim_x);
    d.add_dimensions_lod(dataCube.new_dim_y);
    d.add_dimensions_lod(dataCube.new_dim_z);
    //cout << dataCube.LOD_num_bytes << endl;
    for (int i = 0; i < dataCube.LOD_num_bytes; i += bytes_per_write){
      if ( dataCube.LOD_num_bytes - i < bytes_per_write){
        d.set_bytes(bytes, dataCube.LOD_num_bytes - i);
        d.set_num_bytes(dataCube.LOD_num_bytes - i);
      } else {
        d.set_bytes(bytes, bytes_per_write);
        d.set_num_bytes(bytes_per_write);
        bytes += bytes_per_write; // Update the pointer.
      }
      writer->Write(d);
      //bytes += bytes_per_write; // Update the pointer.
    }
  }

  void streamHQRender(ServerWriter<HQRender> *writer){
    cout << "Streaming HQ Render" << endl;
    char * encodedData = reinterpret_cast<char *>(encodedFramePkt.data);
    //char * encodedData = reinterpret_cast<char *>(pixelData);
    // for ( int i=100000; i<100100; i++){
    //   cout << +*(encodedData+i) << ' ';
    // }
    int num_bytes_tmp  = encodedFramePkt.size;
    //int num_bytes_tmp = number_of_bytes;

    cout << "Starting to write encoded frame to stream: " << bytes_per_write << ' ' << num_bytes_tmp << endl;
    HQRender d;
    d.set_size_in_bytes(num_bytes_tmp);
    d.set_width(window_width);
    d.set_height(window_height);

    for (int i = 0; i < num_bytes_tmp; i += bytes_per_write){
      //cout << "Hello" << endl;
      if ( num_bytes_tmp - i < bytes_per_write){
        cout << num_bytes_tmp - i << endl;
        d.set_bytes(encodedData, num_bytes_tmp - i);
        d.set_num_bytes(num_bytes_tmp - i);
        //encodedData += bytes_per_write; // Update the pointer.
      } else {
        cout << bytes_per_write << endl;
        d.set_bytes(encodedData, bytes_per_write);
        d.set_num_bytes(bytes_per_write);
        encodedData += bytes_per_write; // Update the pointer.
      }
      //encodedData += bytes_per_write; // Update the pointer.
      writer->Write(d);
      // encodedData += bytes_per_write; // Update the pointer.
      // cout << bytes_per_write;
    }
  }

  int Compress(float* array, vector<char>* compression_buffer, size_t* compressed_size, 
               uint32_t nx, uint32_t ny, uint32 nz, uint32_t precision) {
    int status = 0;     /* return value: 0 = success */
    zfp_type type;      /* array scalar type */
    zfp_field* field;   /* array meta data */
    zfp_stream* zfp;    /* compressed stream */
    size_t buffer_size; /* byte size of compressed buffer */
    bitstream* stream;  /* bit stream to write to or read from */

    type = zfp_type_float;
    field = zfp_field_3d(array, type, nx, ny, nz);

    /* allocate meta data for a compressed stream */
    zfp = zfp_stream_open(nullptr);

    /* set compression mode and parameters via one of three functions */
    zfp_stream_set_precision(zfp, precision);

    /* allocate buffer for compressed data */
    buffer_size = zfp_stream_maximum_size(zfp, field);
    if (compression_buffer->size() < buffer_size) {
        compression_buffer->resize(buffer_size);
    }
    stream = stream_open(compression_buffer->data(), buffer_size);
    zfp_stream_set_bit_stream(zfp, stream);
    zfp_stream_rewind(zfp);

    *compressed_size = zfp_compress(zfp, field);
    if (!(*compressed_size)) {
        status = 1;
    }

    /* clean up */
    zfp_field_free(field);
    zfp_stream_close(zfp);
    stream_close(stream);

    return status;
}

};

void RunServer()
{
  std::string server_address("0.0.0.0:50051");
  GreeterServiceImpl service;

  grpc::EnableDefaultHealthCheckService(true);
  grpc::reflection::InitProtoReflectionServerBuilderPlugin();
  ServerBuilder builder;
  // Listen on the given address without any authentication mechanism.
  builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
  // Register "service" as the instance through which we'll communicate with
  // clients. In this case it corresponds to an *synchronous* service.
  builder.RegisterService(&service);
  // Finally assemble the server.
  std::unique_ptr<Server> server(builder.BuildAndStart());
  std::cout << "Server listening on " << server_address << std::endl;

  // Wait for the server to shutdown. Note that some other thread must be
  // responsible for shutting down the server for this call to ever return.
  server->Wait();
}

int main(int argc, char *argv[])
{
  // DataCube dataCube;
  // dataCube.createCube("ds9.arr");
  // renderLODModelOnServer(&dataCube);
  RunServer();
  return 0;
}
