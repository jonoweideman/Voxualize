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

#include "helloworld.grpc.pb.h"

using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::Status;
using helloworld::FileDetails;
using helloworld::Greeter;
using helloworld::HelloReply;

struct DataCube
{
  std::string fileName;
  int dimx, dimy, dimz;
  size_t num_pixels;
  float *array;
};

void readFromFile(DataCube *dataCube, float *array);

void readFrom(DataCube *pCube, vtkSmartPointer<vtkFloatArray> pointer);

void readFromFile(DataCube *dataCube, float *array)
{
  size_t num_pixels = dataCube->dimx * dataCube->dimy * dataCube->dimz;
  std::cout << num_pixels << std::endl;
  std::ifstream input_file(dataCube->fileName, ios::binary);
  std::cout << dataCube->fileName << std::endl;
  input_file.read((char *)array, num_pixels * sizeof(float));
}

int generateImage(std::string inputFile, std::string dimX, std::string dimY, std::string dimZ)
{
  std::string file = inputFile;
  std::cout << file << std::endl;

  // Read raw binary file data into float array
  struct DataCube dataCube;
  dataCube.fileName = inputFile;
  dataCube.dimx = std::stoi(dimX);
  dataCube.dimy = std::stoi(dimY);
  dataCube.dimz = std::stoi(dimZ);
  dataCube.num_pixels = dataCube.dimx * dataCube.dimy * dataCube.dimz;
  std::cout << dataCube.dimx << ' ' << dataCube.dimz << std::endl;
  dataCube.array = new float[dataCube.num_pixels];
  readFromFile(&dataCube, dataCube.array);

  // Construct vtkFloatArray from this float array. No copying is done here
  vtkSmartPointer<vtkFloatArray> floatArray = vtkSmartPointer<vtkFloatArray>::New();
  floatArray->SetName("Float Array");
  floatArray->SetArray(dataCube.array, dataCube.num_pixels, 1);

  // Create vtkImageImport object in order to use an array as input data to the volume mapper.
  vtkSmartPointer<vtkImageImport> imageImport = vtkSmartPointer<vtkImageImport>::New();
  imageImport->SetDataScalarTypeToFloat();
  imageImport->SetWholeExtent(0, dataCube.dimx - 1, 0, dataCube.dimy - 1, 0, dataCube.dimz - 1);
  imageImport->SetDataExtentToWholeExtent();
  imageImport->SetImportVoidPointer(floatArray->GetVoidPointer(0));
  imageImport->SetNumberOfScalarComponents(1);
  imageImport->SetDataSpacing(1.0, double(dataCube.dimx) / dataCube.dimy, (double)dataCube.dimx / dataCube.dimz);
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

class GreeterServiceImpl final : public Greeter::Service
{
  Status SayHello(ServerContext *context, const FileDetails *request,
                  HelloReply *reply) override
  {
    std::string filename(request->filename());
    std::string dimx(request->dimensionx());
    std::string dimy(request->dimensiony());
    std::string dimz(request->dimensionz());

    generateImage(filename, dimx, dimy, dimz);

    return Status::OK;
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
