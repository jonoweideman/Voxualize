#include <fstream>
#include <string>
#include <cstdlib>
#include <vtk-7.1/vtkCamera.h>
#include <vtk-7.1/vtkColorTransferFunction.h>
#include <vtk-7.1/vtkDataArray.h>
#include <vtk-7.1/vtkDataSet.h>
#include <vtk-7.1/vtkFixedPointVolumeRayCastMapper.h>
#include <vtk-7.1/vtkImageData.h>
#include <vtk-7.1/vtkImageImport.h>
#include <vtk-7.1/vtkImageReader.h>
#include <vtk-7.1/vtkImageShiftScale.h>
#include <vtk-7.1/vtkNamedColors.h>
#include <vtk-7.1/vtkPiecewiseFunction.h>
#include <vtk-7.1/vtkPointData.h>
#include <vtk-7.1/vtkRenderer.h>
#include <vtk-7.1/vtkRenderWindow.h>
#include <vtk-7.1/vtkRenderWindowInteractor.h>
#include <vtk-7.1/vtkSmartPointer.h>
#include <vtk-7.1/vtkSmartVolumeMapper.h>
#include <vtk-7.1/vtkStructuredPointsReader.h>
#include <vtk-7.1/vtkVolume.h>
#include <vtk-7.1/vtkVolumeProperty.h>
#include <vtk-7.1/vtkXMLImageDataReader.h>
#include <vtk-7.1/vtkFloatArray.h>
#include <vtk-7.1/vtkImageActor.h>
#include <vtk-7.1/vtkInteractorStyleImage.h>

#include <grpcpp/grpcpp.h>
#include <grpcpp/health_check_service_interface.h>
#include <grpcpp/ext/proto_server_reflection_plugin.h>

#include <grpc/grpc.h>
#include <grpcpp/server.h>
#include <grpcpp/server_builder.h>
#include <grpcpp/server_context.h>

#include "boost/filesystem.hpp"   // includes all needed Boost.Filesystem declarations
#include "boost/filesystem/path.hpp"
#include <iostream>
namespace fs = boost::filesystem;

#include "helloworld.grpc.pb.h"
#include "data_cube.h"

using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::ServerWriter;
using grpc::ServerReader;
using grpc::ServerReaderWriter;
using grpc::Status;
using helloworld::FileDetails;
using helloworld::Greeter;
using helloworld::DataModel;
using helloworld::FilesRequest;
using helloworld::FilesList;

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
  // Service to return the data of a specified file. 
  Status ChooseFile(ServerContext *context, const FileDetails *request,
                  ServerWriter<DataModel> *writer) override {
    std::string file_name(request->file_name());
    DataCube dataCube(file_name);
    //renderFullModelOnServer(&dataCube); // For testing.

    // TO DOs:

    // compression

    // return full array in response as bytes
    //streamFullModel(writer, &dataCube);

    // Or return LOD of array in response as bytes
    float * LODModel = dataCube.generateLODModel();

    renderLODModelOnServer(&dataCube); //For testing.

    //streamLODModel(writer, &dataCube, LODModel);
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

  // Function which receives a path to a directory and a pointer to the FilesList reply.
  // It must add all the file names and their sizes to the response.
  void createFilesListResponse(const fs::path & dir_path, FilesList *reply){
    fs::directory_iterator end_itr; // default construction yields past-the-end
    for ( fs::directory_iterator itr( dir_path ); itr != end_itr; ++itr ){
      if ( fs::is_directory(itr->status()) )
        createFilesListResponse(itr->path(), reply); // Recursive call for subdirectories
      else{
        FilesList::File* file = reply->add_files();
        file->set_file_name(itr->path().leaf().string());
        file->set_file_size((float)fs::file_size(itr->path())/1000000); 
      }
    }
  }

  // Function which streams the full model of a DataCube object
  void streamFullModel(ServerWriter<DataModel> *writer, DataCube *dataCube){
    char * bytes = dataCube->getBytePointerFullModel();
    int bytes_per_write = 64*64*64; // Performance tests to be run on this.
    DataModel d;
    for (int i = 0; i < dataCube->num_bytes; i += bytes_per_write){
      if ( dataCube->num_bytes - i < bytes_per_write){
        d.set_bytes(bytes, dataCube->num_bytes - i);
        d.set_num_bytes(dataCube->num_bytes - i);
      } else {
        d.set_bytes(bytes, bytes_per_write);
        d.set_num_bytes(bytes_per_write);
      }
      writer->Write(d);
      bytes += bytes_per_write; // Update the pointer.
    }
  }

  void streamLODModel(ServerWriter<DataModel> *writer, DataCube *dataCube, float *LODModel){

    
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
  RunServer();
  return 0;
}
