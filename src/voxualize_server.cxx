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
#include <thread>

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

  const int bytes_per_write = 64*64*64; // The number of bytes transported in each 
                                        // protobuf message when streaming data.

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
  std::string uuid;

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
    if (request->target_size_lod_bytes() == 0)
      dataCube.generateLODModel(10);
    else
      dataCube.generateLODModel(request->target_size_lod_bytes());
    if (start_render_thread.joinable())
      start_render_thread.join();
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

    // Get pointer to data.
    pixelData = updateCameraAndGetData(request); //and save screenshot....for now.

    // Encode with FFMPEG.
    AVPacket * pkt = encodePixelData(pixelData, request);

    streamHQRender(writer);
    mtx.unlock();
    return Status::OK;
  }

  Status GetNewROILOD(ServerContext *context, const CameraInfo *request,
                            ServerWriter<ROILOD> *writer) override {
    // Given the cropping planes info, compute new LOD model.
    mtx.lock();
    cout << "GetNewROILOD rpc" << endl;
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
    
    const google::protobuf::RepeatedField<float> cplanes = request->cropping_planes();
    float cropping_dims[6];
    cropping_dims[0] = cplanes.Get(0); cropping_dims[1] = cplanes.Get(1); cropping_dims[2] = cplanes.Get(2);
    cropping_dims[3] = cplanes.Get(3); cropping_dims[4] = cplanes.Get(4); cropping_dims[5] = cplanes.Get(5);

    dataCube.generateLODModelNew(request->target_size_lod_bytes(), &cropping_dims[0]);

    streamLODModel(writer);
    cout << "Finished GetNewROILOD rpc" << endl;
    mtx.unlock();
    return Status::OK;
  }

  // Reset Cropping Planes to full.
  Status Reset(ServerContext *context, const CameraInfo *request,
                            Empty *reply) override {
    dataCube.generateLODModel(request->target_size_lod_bytes());
    return Status::OK;
  }

  // Setup VTK EGL render on server
  void createEGLRenderOnServer(){
    cout << "Setting up VTK objects." << endl;
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
    renWin->GlobalWarningDisplayOff();

    // Create transfer mapping scalar value to opacity
    // Data values for ds9.arr 540x450x201 are in range [-0.139794;0.153026]
    opacityTransferFunction = vtkSmartPointer<vtkPiecewiseFunction>::New();
    opacityTransferFunction->AddPoint(0.0, 0.0);
    opacityTransferFunction->AddPoint(dataCube.max_pixel_val, 1.0);

    // Create transfer mapping scalar value to color
    colorTransferFunction = vtkSmartPointer<vtkColorTransferFunction>::New();
    colorTransferFunction->AddRGBPoint(dataCube.min_pixel_val, 0.0, 0.0, 0.0);
    colorTransferFunction->AddRGBPoint(dataCube.max_pixel_val, 1.0, 1.0, 1.0);

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
    renWin->Render();
    renWin->Frame();
    renWin->WaitForCompletion();

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
    const google::protobuf::RepeatedField<double> opacity_array = request->opacity_array();
    
    opacityTransferFunction->RemoveAllPoints();
    for (int i = 0; i<opacity_array.size(); i = i+2){
      opacityTransferFunction->AddPoint(opacity_array[i], opacity_array[i+1]);
    }

    const float alpha = request->alpha();
    const double distance = request->distance();
    window_width = request->window_width();
    window_height = request->window_height();
    uuid = request->uuid();

    renWin -> Finalize();
    renWin-> Initialize();
    renWin->SetSize(window_width, window_height);

    ren1->GetActiveCamera()->SetPosition(position.Get(0)*dataCube.x_scale_factor,position.Get(1)*dataCube.y_scale_factor,position.Get(2)*dataCube.z_scale_factor);
    ren1->GetActiveCamera()->SetViewUp(view_up.Get(0),view_up.Get(1),view_up.Get(2));
    ren1->GetActiveCamera()->SetFocalPoint(focal_point.Get(0)*dataCube.x_scale_factor,focal_point.Get(1)*dataCube.y_scale_factor,focal_point.Get(2)*dataCube.z_scale_factor);
    colorTransferFunction->RemoveAllPoints();
    colorTransferFunction->AddRGBPoint(0.0, 0.0, 0.0, 0.0);
    colorTransferFunction->AddRGBPoint(0.0, rgb.Get(0)/255, rgb.Get(1)/255, rgb.Get(2)/255);
    colorTransferFunction->AddRGBPoint(round(dataCube.max_pixel_val*10)/10, 1.0, 1.0, 1.0);

    volumeMapper->SetCroppingRegionPlanes(cplanes.Get(0)*dataCube.x_scale_factor, cplanes.Get(1)*dataCube.x_scale_factor, 
                                        cplanes.Get(2)*dataCube.y_scale_factor, cplanes.Get(3)*dataCube.y_scale_factor, 
                                        cplanes.Get(4)*dataCube.z_scale_factor, cplanes.Get(5)*dataCube.z_scale_factor);

    renWin->AddRenderer(ren1);
    renWin->Render();
    renWin->Frame();

    renWin->WaitForCompletion();

    oldSB = renWin->GetSwapBuffers();
    renWin->SwapBuffersOff();

    captureScreenShotOfCurrentEGLRender();

    vtkSmartPointer<vtkWindowToImageFilter> windowToImageFilter = 
      vtkSmartPointer<vtkWindowToImageFilter>::New();
    windowToImageFilter->SetInput(renWin);
    windowToImageFilter->SetInputBufferTypeToRGB(); //also record the alpha (transparency) channel
    windowToImageFilter->ReadFrontBufferOff(); // read from the back buffer
    windowToImageFilter->Update();

    imageData = windowToImageFilter->GetOutput();

    int* dims = imageData->GetDimensions();
    pointer = static_cast<unsigned char *>(imageData->GetScalarPointer(0,0,0));

    renWin->SetSwapBuffers(oldSB);

    return pointer;
  }
  
  // Encode the data using ffmpeg.
  AVPacket * encodePixelData(unsigned char *pixelData, const CameraInfo *request){
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

    i = 0;

    for (got_output = 1; got_output; i++) {
      fflush(stdout);
      ret = avcodec_encode_video2(c, pkt, NULL, &got_output);
      if (got_output) {
        return pkt;
      }
    }

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
    char * bytes = dataCube.getBytePointerLODModel();
    ROILOD d;
    d.set_total_lod_bytes(dataCube.LOD_num_bytes);
    d.add_dimensions_lod(dataCube.new_dim_x);
    d.add_dimensions_lod(dataCube.new_dim_y);
    d.add_dimensions_lod(dataCube.new_dim_z);
    d.set_min_pixel(dataCube.min_pixel_val);
    d.set_max_pixel(dataCube.max_pixel_val);
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
    }
  }

  void streamZfpCompressedLODModel(ServerWriter<ROILOD> *writer){
    //Do zfp compression.
    int status = Compress(dataCube.LODFloatArray,  &compression_buffer, &compression_size, 
                          dataCube.new_dim_x, dataCube.new_dim_y, dataCube.new_dim_z, 11);

    char * bytes = compression_buffer.data();
    ROILOD d;
    d.set_total_lod_bytes(dataCube.LOD_num_bytes);
    d.add_dimensions_lod(dataCube.new_dim_x);
    d.add_dimensions_lod(dataCube.new_dim_y);
    d.add_dimensions_lod(dataCube.new_dim_z);
    for (int i = 0; i < compression_size; i += bytes_per_write){
      if ( compression_size - i < bytes_per_write){
        d.set_bytes(bytes, compression_size - i);
        d.set_num_bytes(compression_size - i);
      } else {
        d.set_bytes(bytes, bytes_per_write);
        d.set_num_bytes(bytes_per_write);
      }
      writer->Write(d);
      bytes += bytes_per_write; // Update the pointer.
    }
  }

  void streamHQRender(ServerWriter<HQRender> *writer){
    cout << "Starting to stream HQ Render" << endl;
    char * encodedData = reinterpret_cast<char *>(encodedFramePkt.data);
    int num_bytes_tmp  = encodedFramePkt.size;

    HQRender d;
    d.set_size_in_bytes(num_bytes_tmp);
    d.set_width(window_width);
    d.set_height(window_height);
    d.set_uuid(uuid);

    for (int i = 0; i < num_bytes_tmp; i += bytes_per_write){
      if ( num_bytes_tmp - i < bytes_per_write){
        cout << num_bytes_tmp - i << endl;
        d.set_bytes(encodedData, num_bytes_tmp - i);
        d.set_num_bytes(num_bytes_tmp - i);
      } else {
        cout << bytes_per_write << endl;
        d.set_bytes(encodedData, bytes_per_write);
        d.set_num_bytes(bytes_per_write);
        encodedData += bytes_per_write; // Update the pointer.
      }
      writer->Write(d);
    }
  }

  // ZFP compression.
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

void Test(){
  int fail = 0;
  DataCube dataCube;
  std::string file_name;

  int dims[3];
  dataCube.fileName = "non-existent_file.arr";
  bool exists = dataCube.getDimensions(dims);
  if (exists == false)
    cout << "Test 1 passed (getDimensions - file not in File_Information.txt" << endl;
  else {
    cout << "Test 1 failed (getDimensions - file not in File_Information.txt" << endl;
    fail++;
  }
  dataCube.fileName = "ds9.arr";
  exists = dataCube.getDimensions(dims);
  if (exists == true)
    cout << "Test 2 passed (getDimensions - file is in File_Information.txt)" << endl;
  else {
    cout << "Test 2 failed (getDimensions - file is in File_Information.txt)" << endl;
    fail++;
  }
  
  file_name = "ds9.arr";
  dataCube.createCube(file_name);
  float * ptr = dataCube.getFloatPointerFullModel();
  if (-(int)(ptr[0]*1000000000)==157670 && -(int)(ptr[1]*1000000000)==649469 && -(int)(ptr[2]*1000000000)==953495)
    cout << "Test 3 passed (createCube)" << endl;
  else {
    cout << "Test 3 failed (createCube)" << endl;
    fail++;
  }
  
  dataCube.generateLODModel(10);
  int cplanes[6] = {0, 100, 0, 100, 20, 100};
  if ((int)(dataCube.calculateMax(4,4,4)*1000000000)==478817)
    cout << "Test 4 passed (calculateMax - full model)" << endl;
  else {
    cout << "Test 4 failed (calculateMax - full model)" << endl;
    fail++;
  }
  if (-(int)(dataCube.calculateMean(4,4,4)*1000000000)==42853)
    cout << "Test 5 passed (calculateMean - full model)" << endl;
  else {
    cout << "Test 5 failed (calculateMean - full model)" << endl;
    fail++;
  }
  if ((int)(dataCube.calculateMax(4,4,4,&cplanes[0])*10000000)==10294)
    cout << "Test 6 passed (calculateMax - with cplanes specified)" << endl;
  else {
    cout << "Test 6 failed (calculateMax - with cplanes specified)" << endl;
    fail++;
  }
  if ((int)(dataCube.calculateMean(4,4,4,&cplanes[0])*1000000000)==53703)
    cout << "Test 7 passed (calculateMean - with cplanes specified)" << endl;
  else {
    cout << "Test 7 failed (calculateMean - with cplanes specified)" << endl;
    fail++;
  }

  float cplanes2[6] = {0, 100, 0, 100, 20, 100};
  dataCube.generateLODModelNew(10, &cplanes2[0]);
  ptr = dataCube.LODFloatArray;

  if (-(int)(ptr[0]*1000000000)==75495 && -(int)(ptr[1]*1000000000)==288029 && -(int)(ptr[2]*1000000000)==240666)
    cout << "Test 8 passed (generateLODModelNew)" << endl;
  else {
    cout << "Test 8 failed (generateLODModelNew)" << endl;
    fail++;
  }

  if (fail>0){
    cout << fail << " tests were failed." << endl;
  } else
    cout << "All tests were passed." << endl;
}

int main(int argc, char *argv[])
{
  string test;
  if (argc >1) 
    test = argv[1];
  if (test.compare("tests")==0){
   Test();
  } else 
    RunServer();

  return 0;
}
