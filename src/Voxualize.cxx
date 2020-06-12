#include <fstream>
#include <string>
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
#include <vtkFloatArray.h>
#include <vtkImageActor.h>
#include <vtkInteractorStyleImage.h>


struct DataCube {
    std::string fileName;
    int dimx, dimy, dimz;
    size_t num_pixels;
    float* array;
};

void readFromFile(DataCube* dataCube, float* array);

void readFrom(DataCube *pCube, vtkSmartPointer<vtkFloatArray> pointer);

int main(int argc, char *argv[])
{
  if (argc < 2)
  {
    std::cout << "Please specify an input file" << std::endl;
  }
  else
  {
    float* array = new float[540*450*201];
    struct DataCube dataCube;
    dataCube.fileName = argv[1];
    dataCube.dimx = 540; dataCube.dimy = 450; dataCube.dimz = 201;
    dataCube.num_pixels = dataCube.dimx * dataCube.dimy * dataCube.dimz;
    dataCube.array = array;
    readFromFile(&dataCube, array);

    vtkSmartPointer<vtkFloatArray> floatArray = vtkSmartPointer<vtkFloatArray>::New();
    floatArray->SetName("Float Array");
    floatArray->SetArray(array, dataCube.num_pixels, 1);

//    float max = -10;
//    float min = 10;
//    for (int i = 0; i < dataCube.num_pixels; i++){
//      float f = floats->GetValue(i);
//      if ( f > max){
//      max = f;}
//      if ( f< min){
//        min = f;
//      }
//    }
//
//    std::cout << max << "   " << min << std::endl;
    //vtkSmartPointer<vtkImageData> imageData = vtkSmartPointer<vtkImageData>::New();
    //imageData->SetDimensions(540,450,201);
    vtkSmartPointer<vtkImageImport> imageImport = vtkSmartPointer<vtkImageImport>::New();
    imageImport -> SetDataScalarTypeToFloat();
    imageImport-> SetWholeExtent(0,540,0,450,0,201);
    imageImport ->SetDataExtentToWholeExtent();
    imageImport->SetImportVoidPointer(floatArray);
    imageImport->SetNumberOfScalarComponents(1);
    //reader->SetInputData(floatArray);
    //imageData->ShallowCopy(reader->GetOutput());
    imageImport -> SetDataSpacing(1,1,1);
    imageImport -> Update();

//    float max = -10;
//    float min = 10;
//    for (int i = 0; i < dataCube.num_pixels; i++){
//      float f = reader->GetInputInformation()
//      if ( f > max){
//      max = f;}
//      if ( f< min){
//        min = f;
//      }
//    }
//
//    std::cout << max << "   " << min << std::endl;


    // Create an actor
    vtkSmartPointer<vtkImageActor> actor =
      vtkSmartPointer<vtkImageActor>::New();
    actor->SetInputData(imageImport->GetOutput());

    // Setup renderer
    vtkSmartPointer<vtkRenderer> renderer =
      vtkSmartPointer<vtkRenderer>::New();
    renderer->AddActor(actor);
    renderer->ResetCamera();

    // Setup render window
    vtkSmartPointer<vtkRenderWindow> renderWindow =
      vtkSmartPointer<vtkRenderWindow>::New();
    renderWindow->AddRenderer(renderer);

    // Setup render window interactor
    vtkSmartPointer<vtkRenderWindowInteractor> renderWindowInteractor =
      vtkSmartPointer<vtkRenderWindowInteractor>::New();
    vtkSmartPointer<vtkInteractorStyleImage> style =
      vtkSmartPointer<vtkInteractorStyleImage>::New();

    renderWindowInteractor->SetInteractorStyle(style);

    // Render and start interaction
    renderWindowInteractor->SetRenderWindow(renderWindow);
    renderWindow->Render();
    renderWindowInteractor->Initialize();

    renderWindowInteractor->Start();

    return EXIT_SUCCESS;
  }
}

void readFromFile(DataCube* dataCube, float* array){
  size_t num_pixels = (*dataCube).dimx*(*dataCube).dimy*(*dataCube).dimz;
  std::cout<<num_pixels<<std::endl;
  std::ifstream input_file((*dataCube).fileName, ios::binary);
  std::cout<<(*dataCube).fileName<<std::endl;
  input_file.read((char*) array, num_pixels * sizeof(float));
}
