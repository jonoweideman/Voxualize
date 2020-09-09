#include <fstream>
#include <string>
#include <cstdlib>
#include <stdio.h>
#include <stdlib.h>
#include <iostream>
#include <chrono>
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
using voxualize::DataModel;
using voxualize::FilesRequest;
using voxualize::FilesList;
using voxualize::CameraInfo;
using voxualize::Dummy;
using voxualize::DimensionDetails;

int renderFullModelOnServer(DataCube *dataCube){
  vtkSmartPointer<vtkFloatArray> floatArray = vtkSmartPointer<vtkFloatArray>::New();
  floatArray->SetName("Float Array");
  floatArray->SetArray((*dataCube).floatArray, (*dataCube).num_pixels, 1);

  // Create vtkImageImport object in order to use an array as input data to the volume mapper.
  vtkSmartPointer<vtkImageImport> imageImport = vtkSmartPointer<vtkImageImport>::New();
  imageImport->SetDataScalarTypeToFloat();
  imageImport->SetWholeExtent(0, dataCube->dimx - 1, 0, dataCube->dimy - 1, 0, dataCube->dimz - 1);
  imageImport->SetDataExtentToWholeExtent();
  imageImport->SetImportVoidPointer(floatArray->GetVoidPointer(0));
  imageImport->SetNumberOfScalarComponents(1);
  imageImport->SetDataSpacing(1.0, (double)(dataCube->dimx) / dataCube->dimy, (double)(dataCube->dimx) / dataCube->dimz);
  imageImport->Update();

  // The mapper / ray cast function know how to render the data
  vtkSmartPointer<vtkSmartVolumeMapper> volumeMapper =
      vtkSmartPointer<vtkSmartVolumeMapper>::New();
  volumeMapper->SetBlendModeToComposite(); // composite first
  volumeMapper->SetInputConnection(imageImport->GetOutputPort());

  // Create the standard renderer, render window
  // and interactor
  vtkSmartPointer<vtkNamedColors> colors =
      vtkSmartPointer<vtkNamedColors>::New();

  vtkSmartPointer<vtkRenderer> ren1 =
      vtkSmartPointer<vtkRenderer>::New();

  vtkSmartPointer<vtkRenderWindow> renWin =
      vtkSmartPointer<vtkRenderWindow>::New();
  renWin->AddRenderer(ren1);

  vtkSmartPointer<vtkRenderWindowInteractor> iren =
      vtkSmartPointer<vtkRenderWindowInteractor>::New();
  iren->SetRenderWindow(renWin);

  // Create transfer mapping scalar value to opacity
  // Data values for ds9.arr 540x450x201 are in range [-0.139794;0.153026]
  vtkSmartPointer<vtkPiecewiseFunction> opacityTransferFunction =
      vtkSmartPointer<vtkPiecewiseFunction>::New();
  //opacityTransferFunction->AddPoint(-0.14, 0.0);
  opacityTransferFunction->AddPoint(-0.0, 0.0);
  opacityTransferFunction->AddPoint(0.16, 1.0);

  // Create transfer mapping scalar value to color
  vtkSmartPointer<vtkColorTransferFunction> colorTransferFunction =
      vtkSmartPointer<vtkColorTransferFunction>::New();
  //colorTransferFunction->AddRGBPoint(-0.14, 0.0, 0.0, 0.0);
  colorTransferFunction->AddRGBPoint(-0.0, 0.0, 0.0, 0.0);
  colorTransferFunction->AddRGBPoint(0.16, 1.0, 1.0, 1.0);
  //colorTransferFunction->AddRGBPoint(0.16, 1.0, 0.0, 0.7);

  // The property describes how the data will look
  vtkSmartPointer<vtkVolumeProperty> volumeProperty =
      vtkSmartPointer<vtkVolumeProperty>::New();
  volumeProperty->SetColor(colorTransferFunction);
  volumeProperty->SetScalarOpacity(opacityTransferFunction);
  //volumeProperty->ShadeOn();
  volumeProperty->SetInterpolationTypeToNearest();

  // The volume holds the mapper and the property and
  // can be used to position/orient the volume
  vtkSmartPointer<vtkVolume> volume =
      vtkSmartPointer<vtkVolume>::New();
  volume->SetMapper(volumeMapper);
  volume->SetProperty(volumeProperty);

  ren1->AddVolume(volume);
  ren1->SetBackground(colors->GetColor3d("Wheat").GetData());
  ren1->GetActiveCamera()->Azimuth(45);
  ren1->GetActiveCamera()->Elevation(30);
  ren1->ResetCameraClippingRange();
  ren1->ResetCamera();

  renWin->SetSize(600, 600);
  renWin->Render();

  iren->Start();

  return EXIT_SUCCESS;
}
  // ------------------------- Codec stuff ----------------------------
  


  // // Screenshot  
  // vtkSmartPointer<vtkWindowToImageFilter> windowToImageFilter1 = 
  //   vtkSmartPointer<vtkWindowToImageFilter>::New();
  // windowToImageFilter1->SetInput(renWin);
  // windowToImageFilter1->SetInputBufferTypeToRGBA(); //also record the alpha (transparency) channel
  // windowToImageFilter1->ReadFrontBufferOff(); // read from the back buffer
  // windowToImageFilter1->Update();
  
  // vtkSmartPointer<vtkPNGWriter> writer1 = 
  //   vtkSmartPointer<vtkPNGWriter>::New();
  // writer1->SetFileName("screenshot1.png");
  // writer1->SetInputConnection(windowToImageFilter1->GetOutputPort());
  // writer1->Write();

  // // Took screenshot. Now change camera position.
  // ren1->GetActiveCamera()->SetPosition(148.27,-100.316,445.291);

  // // Take another screenshot.
  // vtkSmartPointer<vtkWindowToImageFilter> windowToImageFilter2 = 
  //   vtkSmartPointer<vtkWindowToImageFilter>::New();
  // windowToImageFilter2->SetInput(renWin);
  // windowToImageFilter2->SetInputBufferTypeToRGBA(); //also record the alpha (transparency) channel
  // windowToImageFilter2->ReadFrontBufferOff(); // read from the back buffer
  // windowToImageFilter2->Update();

  // vtkSmartPointer<vtkPNGWriter> writer2 = 
  //   vtkSmartPointer<vtkPNGWriter>::New(); 
  // writer2->SetInputConnection(windowToImageFilter2->GetOutputPort()); 
  // writer2->SetFileName("screenshot2.png");
  // writer2->Write();

//   return EXIT_SUCCESS;
// }

int renderLODModelOnServer(DataCube *dataCube){
  vtkSmartPointer<vtkFloatArray> floatArray = vtkSmartPointer<vtkFloatArray>::New();
  floatArray->SetName("Float Array");
  floatArray->SetArray((*dataCube).LODFloatArray, (*dataCube).LOD_num_pixels, 1);

  // Create vtkImageImport object in order to use an array as input data to the volume mapper.
  vtkSmartPointer<vtkImageImport> imageImport = vtkSmartPointer<vtkImageImport>::New();
  imageImport->SetDataScalarTypeToFloat();
  imageImport->SetWholeExtent(0, dataCube->new_dim_x - 1, 0, dataCube->new_dim_y - 1, 0, dataCube->new_dim_z - 1);
  imageImport->SetDataExtentToWholeExtent();
  imageImport->SetImportVoidPointer(floatArray->GetVoidPointer(0));
  imageImport->SetNumberOfScalarComponents(1);
  imageImport->SetDataSpacing(1.0, (double)(dataCube->new_dim_x) / dataCube->new_dim_y, (double)(dataCube->new_dim_x) / dataCube->new_dim_z);
  imageImport->Update();

  // The mapper / ray cast function know how to render the data
  vtkSmartPointer<vtkSmartVolumeMapper> volumeMapper =
      vtkSmartPointer<vtkSmartVolumeMapper>::New();
  volumeMapper->SetBlendModeToComposite(); // composite first
  volumeMapper->SetInputConnection(imageImport->GetOutputPort());

  // Create the standard renderer, render window
  // and interactor
  vtkSmartPointer<vtkNamedColors> colors =
      vtkSmartPointer<vtkNamedColors>::New();

  vtkSmartPointer<vtkRenderer> ren1 =
      vtkSmartPointer<vtkRenderer>::New();

  vtkSmartPointer<vtkRenderWindow> renWin =
      vtkSmartPointer<vtkRenderWindow>::New();
  renWin->AddRenderer(ren1);

  vtkSmartPointer<vtkRenderWindowInteractor> iren =
      vtkSmartPointer<vtkRenderWindowInteractor>::New();
  iren->SetRenderWindow(renWin);

  // Create transfer mapping scalar value to opacity
  // Data values for ds9.arr 540x450x201 are in range [-0.139794;0.153026]
  vtkSmartPointer<vtkPiecewiseFunction> opacityTransferFunction =
      vtkSmartPointer<vtkPiecewiseFunction>::New();
  //opacityTransferFunction->AddPoint(-0.14, 0.0);
  opacityTransferFunction->AddPoint(-0.0, 0.0);
  opacityTransferFunction->AddPoint(0.16, 1.0);

  // Create transfer mapping scalar value to color
  vtkSmartPointer<vtkColorTransferFunction> colorTransferFunction =
      vtkSmartPointer<vtkColorTransferFunction>::New();
  //colorTransferFunction->AddRGBPoint(-0.14, 0.0, 0.0, 0.0);
  colorTransferFunction->AddRGBPoint(-0.0, 0.0, 0.0, 0.0);
  colorTransferFunction->AddRGBPoint(0.16, 1.0, 1.0, 1.0);
  //colorTransferFunction->AddRGBPoint(0.16, 1.0, 0.0, 0.7);

  // The property describes how the data will look
  vtkSmartPointer<vtkVolumeProperty> volumeProperty =
      vtkSmartPointer<vtkVolumeProperty>::New();
  volumeProperty->SetColor(colorTransferFunction);
  volumeProperty->SetScalarOpacity(opacityTransferFunction);
  //volumeProperty->ShadeOn();
  volumeProperty->SetInterpolationTypeToNearest();

  // The volume holds the mapper and the property and
  // can be used to position/orient the volume
  vtkSmartPointer<vtkVolume> volume =
      vtkSmartPointer<vtkVolume>::New();
  volume->SetMapper(volumeMapper);
  volume->SetProperty(volumeProperty);

  ren1->AddVolume(volume);
  ren1->SetBackground(colors->GetColor3d("Wheat").GetData());
  ren1->GetActiveCamera()->Azimuth(45);
  ren1->GetActiveCamera()->Elevation(30);
  ren1->ResetCameraClippingRange();
  ren1->ResetCamera();

  renWin->SetSize(600, 600);
  renWin->Render();

  cout << "Helloooo" << endl;
  iren->Start();

  return EXIT_SUCCESS;
}

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

  long num_bytes;

  // Service to return the data of a specified file. 
  Status ChooseFile(ServerContext *context, const FileDetails *request,
                  DimensionDetails *reply) override {
    std::string file_name(request->file_name());
    dataCube.createCube(file_name);
    
    // The LOD model's dimensions
    DataCube *dc = &dataCube;
    reply->add_dimensions_lod(dc->new_dim_x);
    reply->add_dimensions_lod(dc->new_dim_y);
    reply->add_dimensions_lod(dc->new_dim_z);
    //std::cout<<reply->dimensions_lod(0) << ' ' << reply->dimensions_lod(1) << ' ' << reply->dimensions_lod(2)<< std::endl;

    // The full model's dimensions
    reply->add_dimensions_original(dc->dimx);
    reply->add_dimensions_original(dc->dimy);
    reply->add_dimensions_original(dc->dimz);
    //std::cout<<reply->dimensions_original(0) << ' ' << reply->dimensions_original(1) << ' ' << reply->dimensions_original(2)<< std::endl;
    // The reduction factors of each dimension. This could be calculated from the previous two set's
    // of values, but this is for convenience.
    reply->add_reduction_factors(dc->x_scale_factor);
    reply->add_reduction_factors(dc->y_scale_factor);
    reply->add_reduction_factors(dc->z_scale_factor);

    return Status::OK;
  }

  Status ListFiles(ServerContext *context, const FilesRequest *request,
                  FilesList *reply) override {
    // Retrieve a list of files to view
    std::cout<< "ListFiles request on Server detected" << std::endl;
    fs::path dir_path("../../../Data/");
    createFilesListResponse( dir_path, reply);
    return Status::OK;
  }

  Status GetHighQualityRender(ServerContext *context, const CameraInfo *request,
                            ServerWriter<DataModel> *writer) override {
    // Get pointer to data.
    unsigned char * pixelData = updateCameraAndGetData(request); //and save screenshot....for now.
    
    
    // Encode with NVENC / FFMPEG ? Again return a pointer to encoded data
    AVPacket * pkt = encodePixelData(pixelData, request);
    char * encodedData = reinterpret_cast<char *>(pkt->data);
    int num_bytes_tmp  = pkt->size;

    for (int i = 0; i<100; i+=1){
      cout << encodedData[i] << ' ';
      if ((i+1)%4==0)
        cout << endl;
    }

    // return a stream.
    cout << "Starting to write encoded frame to stream: " << bytes_per_write  << endl;
    DataModel d;
    for (int i = 0; i < num_bytes_tmp; i += bytes_per_write){
      if ( num_bytes_tmp - i < bytes_per_write){
        d.set_bytes(encodedData, num_bytes_tmp - i);
        d.set_num_bytes(num_bytes_tmp - i);
      } else {
        d.set_bytes(encodedData, bytes_per_write);
        d.set_num_bytes(bytes_per_write);
      }
      writer->Write(d);
      encodedData += bytes_per_write; // Update the pointer.
    }
    
    cout << "Finished GetHighQualityRender rpc" << endl;
    return Status::OK;    
  }

  Status GetModelData(ServerContext *context, const Dummy *request,
                            ServerWriter<DataModel> *writer) override {
    //renderFullModelOnServer(&dataCube); // For testing.
    
    // TO DOs:

    // compression of LOD model

    // return full array in response as bytes
    //streamFullModel(writer, &dataCube);

    // Or return LOD model as bytes
    streamLODModel(writer, &dataCube);

    // Backend HQ render to capture screenshots requested by client.
    createEGLRenderOnServer();

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
    imageImport->SetDataSpacing(1.0, (double)(dataCube.dimx) / dataCube.dimy, (double)(dataCube.dimx) / dataCube.dimz);
    imageImport->Update();

    // The mapper / ray cast function know how to render the data
    volumeMapper = vtkSmartPointer<vtkSmartVolumeMapper>::New();
    volumeMapper->SetBlendModeToComposite(); // composite first
    volumeMapper->SetInputConnection(imageImport->GetOutputPort());

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

    ren1->AddVolume(volume);
    ren1->SetBackground(colors->GetColor3d("Wheat").GetData());
    ren1->GetActiveCamera()->Azimuth(45);
    ren1->GetActiveCamera()->Elevation(30);
    ren1->ResetCameraClippingRange();
    ren1->ResetCamera();
    renWin->SwapBuffersOff();
    renWin->SetSize(600, 600);

    renWin->Render();
    renWin->Frame();
    captureScreenShotOfCurrentEGLRender();

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
    const float alpha = request->alpha();
    const double distance = request->distance();


    // Set variables.
    ren1->ResetCamera();
    ren1->GetActiveCamera()->SetPosition(position.Get(0)*dataCube.x_scale_factor,position.Get(1)*dataCube.y_scale_factor,position.Get(2)*dataCube.z_scale_factor);
    ren1->GetActiveCamera()->SetViewUp(view_up.Get(0),view_up.Get(1),view_up.Get(2));
    //ren1->GetActiveCamera()->SetFocalPoint(focal_point.Get(0),focal_point.Get(1),focal_point.Get(2));
    colorTransferFunction->RemoveAllPoints();
    colorTransferFunction->AddRGBPoint(0.0, rgb.Get(0)/255, rgb.Get(1)/255, rgb.Get(2)/255);
    colorTransferFunction->AddRGBPoint(0.16, 1.0, 1.0, 1.0);

    renWin->Render();
    renWin->Frame();
    renWin->WaitForCompletion();

    // int oldSB = renWin->GetSwapBuffers();
    // renWin->SwapBuffersOff();

    captureScreenShotOfCurrentEGLRender();

    vtkSmartPointer<vtkWindowToImageFilter> windowToImageFilter = 
      vtkSmartPointer<vtkWindowToImageFilter>::New();
    windowToImageFilter->SetInput(renWin);
    windowToImageFilter->SetInputBufferTypeToRGBA(); //also record the alpha (transparency) channel
    windowToImageFilter->ReadFrontBufferOff(); // read from the back buffer
    windowToImageFilter->Update();

    // restore swapping state
    // renWin->SetSwapBuffers(oldSB);

    imageData = windowToImageFilter->GetOutput();

    int* dims = imageData->GetDimensions();
    std::cout << "Dims: " << " x: " << dims[0] << " y: " << dims[1] << " z: " << dims[2] << std::endl;

    unsigned char * pointer = static_cast<unsigned char *>(imageData->GetScalarPointer(0,0,0));

    return pointer;
  }
  
  // Encode the data. For now using ffmpeg. Hopefully in future done using GPU acceleration.
  AVPacket * encodePixelData(unsigned char *pixelData, const CameraInfo *request){\
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
    c->width = 600;
    c->height = 600;
    /* frames per second */
    c->time_base = (AVRational){1, 1};
    //c->framerate = (AVRational){1, 1};

    c->gop_size = 0; //Intra only - meaning the pictures should be constructed only from information
                      // within that picture, and not any other pictures.
    c->max_b_frames = 1;
    c->pix_fmt = AV_PIX_FMT_YUV420P;

    if (codec->id == AV_CODEC_ID_H264)
      av_opt_set(c->priv_data, "preset", "slow", 0);

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
                                      AV_PIX_FMT_RGBA, c->width, c->height,
                                      AV_PIX_FMT_YUV420P, 0, 0, 0, 0);

    av_init_packet(pkt);
    pkt->data = NULL;    // packet data will be allocated by the encoder
    pkt->size = 0;

    fflush(stdout);

    uint8_t * inData[1] = { rgba32Data }; // RGBA32 have one plane

    int inLinesize[1] = { 4*c->width }; // RGBA stride

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
        return pkt;
        fwrite(pkt->data, 1, pkt->size, f);
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
    windowToImageFilter->SetInputBufferTypeToRGBA(); //also record the alpha (transparency) channel
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

  // Function which streams the full model of a DataCube object
  void streamFullModel(ServerWriter<DataModel> *writer, DataCube *dataCube){
    char * bytes = dataCube->getBytePointerFullModel();
    int bytes_per_write = 64*64*64; // Performance tests to be run on this.
    DataModel d;
    d.set_num_bytes(dataCube->num_bytes);
    d.set_bytes(bytes, dataCube->num_bytes);
    writer->Write(d);

    float * floats = dataCube->getFloatPointerFullModel();
    for (int i = 0; i<10; i ++){
      cout << *(floats+i) << endl;
    }

    // for (int i = 0; i < dataCube->num_bytes; i += bytes_per_write){
    //   if ( dataCube->num_bytes - i < bytes_per_write){
    //     d.set_bytes(bytes, dataCube->num_bytes - i);
    //     d.set_num_bytes(dataCube->num_bytes - i);
    //   } else {
    //     d.set_bytes(bytes, bytes_per_write);
    //     d.set_num_bytes(bytes_per_write);
    //   }
    //   writer->Write(d);
    //   bytes += bytes_per_write; // Update the pointer.
    // }
  }

  void streamLODModel(ServerWriter<DataModel> *writer, DataCube *dataCube){
    char * bytes = dataCube->getBytePointerLODModel();
    //bytes_per_write = 64*64*64; // Performance tests to be run on this.
    DataModel d;
    for (int i = 0; i < dataCube->LOD_num_bytes; i += bytes_per_write){
      if ( dataCube->LOD_num_bytes - i < bytes_per_write){
        d.set_bytes(bytes, dataCube->LOD_num_bytes - i);
        d.set_num_bytes(dataCube->LOD_num_bytes - i);
      } else {
        d.set_bytes(bytes, bytes_per_write);
        d.set_num_bytes(bytes_per_write);
      }
      writer->Write(d);
      bytes += bytes_per_write; // Update the pointer.
    }
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