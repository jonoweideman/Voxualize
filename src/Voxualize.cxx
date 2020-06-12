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
#include <vtk-7.1/vtkFloatArray.h>
#include <vtk-7.1/vtkImageActor.h>
#include <vtk-7.1/vtkInteractorStyleImage.h>


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
    // Read raw binary file data into float array
    float* array = new float[540*450*201];
    struct DataCube dataCube;
    dataCube.fileName = argv[1];
    dataCube.dimx = 540; dataCube.dimy = 450; dataCube.dimz = 201;
    dataCube.num_pixels = dataCube.dimx * dataCube.dimy * dataCube.dimz;
    dataCube.array = array;
    readFromFile(&dataCube, array);

    // Construct vtkFloatArray from this float array. No copying is done here
    vtkSmartPointer<vtkFloatArray> floatArray = vtkSmartPointer<vtkFloatArray>::New();
    floatArray->SetName("Float Array");
    floatArray->SetArray(array, dataCube.num_pixels, 1);

    // Create vtkImageImport object in order to use an array as input data to the volume mapper.
    vtkSmartPointer<vtkImageImport> imageImport = vtkSmartPointer<vtkImageImport>::New();
    imageImport -> SetDataScalarTypeToFloat();
    imageImport-> SetWholeExtent(0,256,0,256,0,159);
    //imageImport->SetDataExtent(0, 100, 0, 100, 0 ,50);
    imageImport ->SetDataExtentToWholeExtent(); // This line causes "Segmentation fault (core dumped)" when attempting 3D render.

    imageImport->SetImportVoidPointer(floatArray->GetVoidPointer(0)); // Not sure what valueIdx to use.
                                                    // This is probably the issue. This function requires a void *.
                                                    // Other options are SetInputConnection(vtkAlgorithmOutput*) and SetInputData(vtkDataObject*)
                                                    // NOTE: vtkFloatArray is not a vtkDataObject apparently.
    imageImport->SetNumberOfScalarComponents(1);
    imageImport -> SetDataSpacing(1,1,1); // Default values
    imageImport -> Update();

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
    opacityTransferFunction->AddPoint(-0.14, 0.0);
    opacityTransferFunction->AddPoint(0.16, 1.0);

    // Create transfer mapping scalar value to color
    vtkSmartPointer<vtkColorTransferFunction> colorTransferFunction =
      vtkSmartPointer<vtkColorTransferFunction>::New();
    colorTransferFunction->AddRGBPoint(-0.14, 0.0, 0.0, 0.0);
    colorTransferFunction->AddRGBPoint(0.16, 1.0, 1.0, 1.0);
    //colorTransferFunction->AddRGBPoint(0.16, 1.0, 0.0, 0.7);

    // The property describes how the data will look
    vtkSmartPointer<vtkVolumeProperty> volumeProperty =
      vtkSmartPointer<vtkVolumeProperty>::New();
    volumeProperty->SetColor(colorTransferFunction);
    volumeProperty->SetScalarOpacity(opacityTransferFunction);
    volumeProperty->ShadeOn();
    volumeProperty->SetInterpolationTypeToLinear();

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
    // Create an actor
//    vtkSmartPointer<vtkImageActor> actor =
//      vtkSmartPointer<vtkImageActor>::New();
//    actor->SetInputData(imageImport->GetOutput());
//
//    // Setup renderer
//    vtkSmartPointer<vtkRenderer> renderer =
//      vtkSmartPointer<vtkRenderer>::New();
//    renderer->AddActor(actor);
//    renderer->ResetCamera();
//
//    // Setup render window
//    vtkSmartPointer<vtkRenderWindow> renderWindow =
//      vtkSmartPointer<vtkRenderWindow>::New();
//    renderWindow->AddRenderer(renderer);
//
//    // Setup render window interactor
//    vtkSmartPointer<vtkRenderWindowInteractor> renderWindowInteractor =
//      vtkSmartPointer<vtkRenderWindowInteractor>::New();
//    vtkSmartPointer<vtkInteractorStyleImage> style =
//      vtkSmartPointer<vtkInteractorStyleImage>::New();
//
//    renderWindowInteractor->SetInteractorStyle(style);
//
//    // Render and start interaction
//    renderWindowInteractor->SetRenderWindow(renderWindow);
//    renderWindow->Render();
//    renderWindowInteractor->Initialize();
//
//    renderWindowInteractor->Start();
//
//    return EXIT_SUCCESS;
  }
}

void readFromFile(DataCube* dataCube, float* array){
  size_t num_pixels = (*dataCube).dimx*(*dataCube).dimy*(*dataCube).dimz;
  std::cout<<num_pixels<<std::endl;
  std::ifstream input_file((*dataCube).fileName, ios::binary);
  std::cout<<(*dataCube).fileName<<std::endl;
  input_file.read((char*) array, num_pixels * sizeof(float));
}
