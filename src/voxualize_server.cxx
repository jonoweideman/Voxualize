#include <fstream>
#include <string>
#include <cstdlib>
#include <stdio.h>
#include <stdlib.h>

// ffmpeg
extern "C" {
  #include <libavcodec/avcodec.h>

  #include <libavutil/opt.h>
  #include <libavutil/imgutils.h>
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

  vtkSmartPointer<vtkEGLRenderWindow> renWin =
      vtkSmartPointer<vtkEGLRenderWindow>::New();
  renWin->AddRenderer(ren1);

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
  std::cout << "11"<<std::endl;
  renWin->Render();
  std::cout << "22"<<std::endl;

  
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

  return EXIT_SUCCESS;
}

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
    std::cout<<reply->dimensions_lod(0) << ' ' << reply->dimensions_lod(1) << ' ' << reply->dimensions_lod(2)<< std::endl;

    // The full model's dimensions
    reply->add_dimensions_original(dc->dimx);
    reply->add_dimensions_original(dc->dimy);
    reply->add_dimensions_original(dc->dimz);
    std::cout<<reply->dimensions_original(0) << ' ' << reply->dimensions_original(1) << ' ' << reply->dimensions_original(2)<< std::endl;
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
    //For now, log the camera info.
    const google::protobuf::RepeatedField<float> position = request->position();
    std::cout<<"Position: " << position.Get(0) <<' ' << position.Get(1) << ' ' <<position.Get(2) << std::endl;

    const google::protobuf::RepeatedField<float> focal_point = request->focal_point();
    std::cout<<"Focal Point: " << focal_point.Get(0) <<' ' << focal_point.Get(1) << ' ' <<focal_point.Get(2) << std::endl;
    
    // Get pointer to data?
    float * pixelData = updateCameraAndGetData(request);
    
    // Encode with NVENC / FFMPEG ? Again return a pointer to encoded data
    unsigned char * encodedData = encodePixelData(pixelData, request);

    // Write to message ?
    // return a stream.
    char * bytes = reinterpret_cast<char*>(encodedData);

    //bytes_per_write = 64*64*64; // Performance tests to be run on this.
    DataModel d;
    for (int i = 0; i < num_bytes; i += bytes_per_write){
      if ( num_bytes - i < bytes_per_write){
        d.set_bytes(bytes, num_bytes - i);
        d.set_num_bytes(num_bytes - i);
      } else {
        d.set_bytes(bytes, bytes_per_write);
        d.set_num_bytes(bytes_per_write);
      }
      writer->Write(d);
      bytes += bytes_per_write; // Update the pointer.
    }
    return Status::OK;    
  }

  Status GetModelData(ServerContext *context, const Dummy *request,
                            ServerWriter<DataModel> *writer) override {
    //renderFullModelOnServer(&dataCube); // For testing.
    
    // TO DOs:

    // compression

    // return full array in response as bytes
    //streamFullModel(writer, &dataCube);

    // Or return LOD of array in response as bytes
    //dataCube.generateLODModel();

    //renderLODModelOnServer(&dataCube); //For testing.
    streamLODModel(writer, &dataCube);

    createEGLRenderOnServer(); // Backend HQ render

    return Status::OK;
  }

  void createEGLRenderOnServer(){
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
    renWin->AddRenderer(ren1);

    // Create transfer mapping scalar value to opacity
    // Data values for ds9.arr 540x450x201 are in range [-0.139794;0.153026]
    opacityTransferFunction = vtkSmartPointer<vtkPiecewiseFunction>::New();
    //opacityTransferFunction->AddPoint(-0.14, 0.0);
    opacityTransferFunction->AddPoint(-0.0, 0.0);
    opacityTransferFunction->AddPoint(0.16, 1.0);

    // Create transfer mapping scalar value to color
    colorTransferFunction = vtkSmartPointer<vtkColorTransferFunction>::New();
    //colorTransferFunction->AddRGBPoint(-0.14, 0.0, 0.0, 0.0);
    colorTransferFunction->AddRGBPoint(-0.0, 0.0, 0.0, 0.0);
    colorTransferFunction->AddRGBPoint(0.16, 1.0, 1.0, 1.0);
    //colorTransferFunction->AddRGBPoint(0.16, 1.0, 0.0, 0.7);

    // The property describes how the data will look
    volumeProperty = vtkSmartPointer<vtkVolumeProperty>::New();
    volumeProperty->SetColor(colorTransferFunction);
    volumeProperty->SetScalarOpacity(opacityTransferFunction);
    //volumeProperty->ShadeOn();
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

    renWin->SetSize(600, 600);
    renWin->Render();

    return;
  }

  // Get's the pixel data (pointer to it) from the current render on the backend.
  // Also uses information from the request to update the cameras position, get resolution, etc.
  float * updateCameraAndGetData(const CameraInfo *request){
    const google::protobuf::RepeatedField<float> position = request->position();
    const google::protobuf::RepeatedField<float> focal_point = request->focal_point();
    ren1->GetActiveCamera()->SetPosition(position.Get(0),position.Get(1),position.Get(2));
    ren1->GetActiveCamera()->SetFocalPoint(focal_point.Get(0),focal_point.Get(1),focal_point.Get(2));

    return renWin->GetRGBAPixelData(0,0,599,599,0);
    //return renWind->GetPixelData(0,0,599,599,0); //Need to experiement
  }
  
  // Encode the data. For now using ffmpeg. Hopefully in future done using GPU acceleration.
  unsigned char * encodePixelData(float *pixelData, const CameraInfo *request){
    const AVCodec *codec;
    AVCodecContext *c= NULL;
    int i, ret, x, y;
    const char *filename, *codec_name = "libx264";
    AVFrame *frame;
    AVPacket *pkt;

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

    pkt = av_packet_alloc();
    if (!pkt)
      exit(1);
    
    /* put sample parameters */
    c->bit_rate = 400000;
    /* resolution must be a multiple of two */
    c->width = 600;
    c->height = 600;
    /* frames per second */
    c->time_base = (AVRational){1, 1};
    c->framerate = (AVRational){1, 1};

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
    std::cout << frame->linesize[0] << std::endl;
    std::cout << frame->linesize[1] << std::endl;
    std::cout << frame->linesize[2] << std::endl;

    frame->format = c->pix_fmt;
    frame->width  = c->width;
    frame->height = c->height;

    std::cout << frame->linesize[0] << std::endl;
    std::cout << frame->linesize[1] << std::endl;
    std::cout << frame->linesize[2] << std::endl;
    

    ret = av_frame_get_buffer(frame, 0);
    if (ret < 0) {
      fprintf(stderr, "Could not allocate the video frame data\n");
      exit(1);
    }

    /* make sure the frame data is writable */
    ret = av_frame_make_writable(frame);
    if (ret < 0)
      exit(1);
    
    frame->data[0] = reinterpret_cast<uint8_t*>(pixelData);
    frame->pts = 0; // only one frame

    /* encode the image */
    encode(c, frame, pkt);
    
    num_bytes = pkt->size;
    std::cout << num_bytes << std::endl;
    std::cout << frame->linesize[0]*frame->height << std::endl;
    return pkt->data;
  }

  void encode(AVCodecContext* c, AVFrame* frame, AVPacket* pkt){
    int ret = avcodec_send_frame(c, frame);
    if (ret < 0) {
      fprintf(stderr, "Error sending a frame for encoding\n");
      exit(1);
    }

    while (ret >= 0) {
      ret = avcodec_receive_packet(c, pkt);
      if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
        return;
      else if (ret < 0) {
        fprintf(stderr, "Error during encoding\n");
        exit(1);
      }

      printf("Write packet %3"PRId64" (size=%5d)\n", pkt->pts, pkt->size);
      av_packet_unref(pkt);
    }
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
  // renderFullModelOnServer(&dataCube);
  RunServer();
  return 0;
}
