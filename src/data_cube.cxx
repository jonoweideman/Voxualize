#include <string>
#include <fstream>
#include <iostream>
#include <sstream>
#include <cstdlib>
#include "data_cube.h"

// "Default" constructor
DataCube::DataCube(std::string fileName){
  std::cout << "Constructing DataCube object from " + fileName + " file." << std::endl;
  (*this).fileName = fileName;
  readInData();
}

void DataCube::readInData(){
  if (fileName.find('.') > -1){
    std::string fileSuffix(fileName.substr(fileName.find('.')));
    if (fileSuffix == ".fits"){
      //Read FITS file. Don't need to search in File_Information.txt
      readFitsFile();
    } else if (fileSuffix == ".arr") {
      // Read raw file. Need to search in File_information.txt
      readRawFile();
    } else if (fileSuffix == ".vtk") {
      // Read vtk. I assume it has dimensions in the file.
      readVtkFile();
    } else {
      // Undetected file extension, attempt to read it as raw...
      readRawFile();
    } 
  } else {
    // No file extension. Attempt raw read in
    readRawFile();
  }
}

void DataCube::readRawFile(){
  // Need dimensions
  int dims[3];
  if (getDimensions(&dims[0])){
    dimx = dims[0]; dimy = dims[1]; dimz = dims[2];
    num_pixels = dimx * dimy * dimz;
    num_bytes = num_pixels * sizeof(float);
    if (dataType == "float"){ 
      floatArray = new float[num_pixels];
      std::ifstream input_file("../../../Data/" + fileName, std::ios::binary);
      input_file.read((char *)floatArray, num_pixels * sizeof(float));
      constructedCorrectly = true;
      return;
    }
    else if (dataType == "int"){
      intArray = new int[num_pixels];
      std::ifstream input_file("../../../Data/" + fileName, std::ios::binary);
      input_file.read((char *)intArray, num_pixels * sizeof(int));
      constructedCorrectly = true;
      return;
    }
    else { 
      std::cerr<< "Unrecognized data type." << std::endl;
      constructedCorrectly = false;
      return;
    }


  } else {
    std::cerr<< "Unrecognized file type or is raw input and dimensions not specified in Data/File_Information" << std::endl;
    constructedCorrectly = false;
  }
}

void DataCube::readFitsFile(){
  // Do not need dimensions
  return;
}

void DataCube::readVtkFile(){
  // Do not need dimensions
  return;
}

bool DataCube::getDimensions(int *dims){
  // If filename is in File_Information.txt, return true.
  std::ifstream infile("../../../Data/File_Information.txt");
  std::string line;

  while (std::getline(infile, line)){
    if (line[0] == '#') {continue;} // Line is a comment

    std::istringstream iss(line);
    std::string tempFileName;
    std::string tempDataType;
    
    iss >> tempFileName;
    iss >> tempDataType;
    iss >> *dims; iss >> *(dims+1); iss >> *(dims+2);

    if (tempFileName == fileName){
      // Found the file.
      std::cout << "Found the file in File_Information.txt" << std::endl;
      dataType = tempDataType;
      return true;
    } else {
      continue;
    }
  }
  return false; // Could not find file.
}

char * DataCube::getBytePointer(){
  return reinterpret_cast<char *>(floatArray);
}