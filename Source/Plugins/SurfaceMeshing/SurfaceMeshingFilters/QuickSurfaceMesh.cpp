/* ============================================================================
 * Copyright (c) 2009-2016 BlueQuartz Software, LLC
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 * Redistributions of source code must retain the above copyright notice, this
 * list of conditions and the following disclaimer.
 *
 * Redistributions in binary form must reproduce the above copyright notice, this
 * list of conditions and the following disclaimer in the documentation and/or
 * other materials provided with the distribution.
 *
 * Neither the name of BlueQuartz Software, the US Air Force, nor the names of its
 * contributors may be used to endorse or promote products derived from this software
 * without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE
 * USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * The code contained herein was partially funded by the followig contracts:
 *    United States Air Force Prime Contract FA8650-07-D-5800
 *    United States Air Force Prime Contract FA8650-10-D-5210
 *    United States Prime Contract Navy N00173-07-C-2068
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

#include "QuickSurfaceMesh.h"

#include <array>
#include <random>
#include <set>
#include <unordered_map>
#include <unordered_set>

#include "SIMPLib/Common/Constants.h"
#include "SIMPLib/Common/TemplateHelpers.h"
#include "SIMPLib/DataArrays/DynamicListArray.hpp"
#include "SIMPLib/FilterParameters/AbstractFilterParametersReader.h"
#include "SIMPLib/FilterParameters/DataArraySelectionFilterParameter.h"
#include "SIMPLib/FilterParameters/DataContainerCreationFilterParameter.h"
#include "SIMPLib/FilterParameters/LinkedBooleanFilterParameter.h"
#include "SIMPLib/FilterParameters/MultiDataArraySelectionFilterParameter.h"
#include "SIMPLib/FilterParameters/SeparatorFilterParameter.h"
#include "SIMPLib/FilterParameters/StringFilterParameter.h"
#include "SIMPLib/Geometry/EdgeGeom.h"
#include "SIMPLib/Geometry/ImageGeom.h"
#include "SIMPLib/Geometry/TriangleGeom.h"
#include "SIMPLib/Math/SIMPLibRandom.h"

#include "SurfaceMeshing/SurfaceMeshingConstants.h"
#include "SurfaceMeshing/SurfaceMeshingVersion.h"

enum createdPathID : RenameDataPath::DataID_t
{
  AttributeMatrixID21 = 21,
  AttributeMatrixID22 = 22,
  AttributeMatrixID23 = 23,

  DataArrayID31 = 31,
  DataArrayID32 = 32,

  DataContainerID = 1
};

namespace
{
template <class T> inline void hashCombine(size_t& seed, const T& obj)
{
  std::hash<T> hasher;
  seed ^= hasher(obj) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
}

using Vertex = std::array<float, 3>;
using Edge = std::array<int64_t, 2>;

struct VertexHasher
{
  size_t operator()(const Vertex& vert) const
  {
    size_t hash = std::hash<float>()(vert[0]);
    hashCombine(hash, vert[1]);
    hashCombine(hash, vert[2]);
    return hash;
  }
};

struct EdgeHasher
{
  size_t operator()(const Edge& edge) const
  {
    size_t hash = std::hash<int64_t>()(edge[0]);
    hashCombine(hash, edge[1]);
    return hash;
  }
};

using VertexMap = std::unordered_map<Vertex, int64_t, VertexHasher>;
using EdgeMap = std::unordered_map<Edge, int64_t, EdgeHasher>;
} // namespace

// -----------------------------------------------------------------------------
//
// -----------------------------------------------------------------------------
QuickSurfaceMesh::QuickSurfaceMesh()
: m_SelectedDataArrayPaths(QVector<DataArrayPath>())
, m_SurfaceDataContainerName(SIMPL::Defaults::TriangleDataContainerName)
, m_TripleLineDataContainerName("TripleLines")
, m_VertexAttributeMatrixName(SIMPL::Defaults::VertexAttributeMatrixName)
, m_FaceAttributeMatrixName(SIMPL::Defaults::FaceAttributeMatrixName)
, m_FeatureIdsArrayPath(SIMPL::Defaults::ImageDataContainerName, SIMPL::Defaults::CellAttributeMatrixName, SIMPL::CellData::FeatureIds)
, m_FaceLabelsArrayName(SIMPL::FaceData::SurfaceMeshFaceLabels)
, m_NodeTypesArrayName(SIMPL::VertexData::SurfaceMeshNodeType)
, m_FeatureAttributeMatrixName(SIMPL::Defaults::FaceFeatureAttributeMatrixName)
{
}

// -----------------------------------------------------------------------------
//
// -----------------------------------------------------------------------------
QuickSurfaceMesh::~QuickSurfaceMesh() = default;

// -----------------------------------------------------------------------------
//
// -----------------------------------------------------------------------------
void QuickSurfaceMesh::setupFilterParameters()
{
  FilterParameterVectorType parameters;
  parameters.push_back(SeparatorFilterParameter::New("Cell Data", FilterParameter::RequiredArray));
  {
    DataArraySelectionFilterParameter::RequirementType req = DataArraySelectionFilterParameter::CreateRequirement(SIMPL::TypeNames::Int32, 1, AttributeMatrix::Type::Cell, IGeometry::Type::Any);
    IGeometry::Types geomTypes = {IGeometry::Type::Image, IGeometry::Type::RectGrid};
    req.dcGeometryTypes = geomTypes;
    parameters.push_back(SIMPL_NEW_DA_SELECTION_FP("Feature Ids", FeatureIdsArrayPath, FilterParameter::RequiredArray, QuickSurfaceMesh, req));
  }
  {
    MultiDataArraySelectionFilterParameter::RequirementType req =
        MultiDataArraySelectionFilterParameter::CreateRequirement(SIMPL::Defaults::AnyPrimitive, SIMPL::Defaults::AnyComponentSize, AttributeMatrix::Type::Cell, IGeometry::Type::Any);
    IGeometry::Types geomTypes = {IGeometry::Type::Image, IGeometry::Type::RectGrid};
    req.dcGeometryTypes = geomTypes;
    parameters.push_back(SIMPL_NEW_MDA_SELECTION_FP("Attribute Arrays to Transfer", SelectedDataArrayPaths, FilterParameter::RequiredArray, QuickSurfaceMesh, req));
  }
  parameters.push_back(SIMPL_NEW_DC_CREATION_FP("Data Container", SurfaceDataContainerName, FilterParameter::CreatedArray, QuickSurfaceMesh));
  parameters.push_back(SeparatorFilterParameter::New("Vertex Data", FilterParameter::CreatedArray));
  parameters.push_back(SIMPL_NEW_STRING_FP("Vertex Attribute Matrix", VertexAttributeMatrixName, FilterParameter::CreatedArray, QuickSurfaceMesh));
  parameters.push_back(SIMPL_NEW_STRING_FP("Node Types", NodeTypesArrayName, FilterParameter::CreatedArray, QuickSurfaceMesh));
  parameters.push_back(SeparatorFilterParameter::New("Face Data", FilterParameter::CreatedArray));
  parameters.push_back(SIMPL_NEW_STRING_FP("Face Attribute Matrix", FaceAttributeMatrixName, FilterParameter::CreatedArray, QuickSurfaceMesh));
  parameters.push_back(SIMPL_NEW_STRING_FP("Face Labels", FaceLabelsArrayName, FilterParameter::CreatedArray, QuickSurfaceMesh));
  parameters.push_back(SeparatorFilterParameter::New("Face Feature Data", FilterParameter::CreatedArray));
  parameters.push_back(SIMPL_NEW_STRING_FP("Face Feature Attribute Matrix", FeatureAttributeMatrixName, FilterParameter::CreatedArray, QuickSurfaceMesh));
  setFilterParameters(parameters);
}

// -----------------------------------------------------------------------------
//
// -----------------------------------------------------------------------------
void QuickSurfaceMesh::readFilterParameters(AbstractFilterParametersReader* reader, int index)
{
  reader->openFilterGroup(this, index);
  setSelectedDataArrayPaths(reader->readDataArrayPathVector("SelectedDataArrayPaths", getSelectedDataArrayPaths()));
  setSurfaceDataContainerName(reader->readDataArrayPath("SurfaceDataContainerName", getSurfaceDataContainerName()));
  setVertexAttributeMatrixName(reader->readString("VertexAttributeMatrixName", getVertexAttributeMatrixName()));
  setFaceAttributeMatrixName(reader->readString("FaceAttributeMatrixName", getFaceAttributeMatrixName()));
  setNodeTypesArrayName(reader->readString("NodeTypesArrayName", getNodeTypesArrayName()));
  setFaceLabelsArrayName(reader->readString("FaceLabelsArrayName", getFaceLabelsArrayName()));
  setFeatureIdsArrayPath(reader->readDataArrayPath("FeatureIdsArrayPath", getFeatureIdsArrayPath()));
  setFeatureAttributeMatrixName(reader->readString("FeatureAttributeMatrixName", getFeatureAttributeMatrixName()));
  reader->closeFilterGroup();
}

// -----------------------------------------------------------------------------
//
// -----------------------------------------------------------------------------
void QuickSurfaceMesh::updateVertexInstancePointers()
{
  clearErrorCode();
  clearWarningCode();
  if(nullptr != m_NodeTypesPtr.lock()) /* Validate the Weak Pointer wraps a non-nullptr pointer to a DataArray<T> object */
  {
    m_NodeTypes = m_NodeTypesPtr.lock()->getPointer(0);
  } /* Now assign the raw pointer to data from the DataArray<T> object */
}

// -----------------------------------------------------------------------------
//
// -----------------------------------------------------------------------------
void QuickSurfaceMesh::updateFaceInstancePointers()
{
  clearErrorCode();
  clearWarningCode();
  if(nullptr != m_FaceLabelsPtr.lock()) /* Validate the Weak Pointer wraps a non-nullptr pointer to a DataArray<T> object */
  {
    m_FaceLabels = m_FaceLabelsPtr.lock()->getPointer(0);
  } /* Now assign the raw pointer to data from the DataArray<T> object */
}

// -----------------------------------------------------------------------------
//
// -----------------------------------------------------------------------------
template <typename T>
void copyCellArraysToFaceArrays(size_t faceIndex, size_t firstcIndex, size_t secondcIndex, IDataArray::Pointer cellArray, IDataArray::Pointer faceArray, bool forceSecondToZero = false)
{
  typename DataArray<T>::Pointer cellPtr = std::dynamic_pointer_cast<DataArray<T>>(cellArray);
  typename DataArray<T>::Pointer facePtr = std::dynamic_pointer_cast<DataArray<T>>(faceArray);

  int32_t numComps = cellPtr->getNumberOfComponents();

  T* faceTuplePtr = facePtr->getTuplePointer(faceIndex);
  T* firstCellTuplePtr = cellPtr->getTuplePointer(firstcIndex);
  T* secondCellTuplePtr = cellPtr->getTuplePointer(secondcIndex);

  ::memcpy(faceTuplePtr, firstCellTuplePtr, sizeof(T) * numComps);
  if(!forceSecondToZero)
  {
    ::memcpy(faceTuplePtr + numComps, secondCellTuplePtr, sizeof(T) * numComps);
  }
}

// -----------------------------------------------------------------------------
//
// -----------------------------------------------------------------------------
void QuickSurfaceMesh::initialize()
{
  m_SelectedWeakPtrVector.clear();
  m_CreatedWeakPtrVector.clear();
}

// -----------------------------------------------------------------------------
//
// -----------------------------------------------------------------------------
void QuickSurfaceMesh::dataCheck()
{
  clearErrorCode();
  clearWarningCode();
  initialize();

  DataArrayPath tempPath;

  getDataContainerArray()->getPrereqGeometryFromDataContainer<IGeometryGrid, AbstractFilter>(this, getFeatureIdsArrayPath().getDataContainerName());

  QVector<DataArrayPath> dataArrayPaths;

  QVector<size_t> cDims(1, 1);
  m_FeatureIdsPtr = getDataContainerArray()->getPrereqArrayFromPath<DataArray<int32_t>, AbstractFilter>(this, getFeatureIdsArrayPath(),
                                                                                                        cDims); /* Assigns the shared_ptr<> to an instance variable that is a weak_ptr<> */
  if(nullptr != m_FeatureIdsPtr.lock())                                                                         /* Validate the Weak Pointer wraps a non-nullptr pointer to a DataArray<T> object */
  {
    m_FeatureIds = m_FeatureIdsPtr.lock()->getPointer(0);
  } /* Now assign the raw pointer to data from the DataArray<T> object */
  if(getErrorCode() >= 0)
  {
    dataArrayPaths.push_back(getFeatureIdsArrayPath());
  }

  getDataContainerArray()->validateNumberOfTuples<AbstractFilter>(this, dataArrayPaths);

  QVector<DataArrayPath> paths = getSelectedDataArrayPaths();

  if(!DataArrayPath::ValidateVector(paths))
  {
    QString ss = QObject::tr("There are Attribute Arrays selected that are not contained in the same Attribute Matrix. All selected Attribute Arrays must belong to the same Attribute Matrix");
    setErrorCondition(-11004, ss);
  }

  for(const auto& path : paths)
  {
    IDataArray::WeakPointer ptr = getDataContainerArray()->getPrereqIDataArrayFromPath<IDataArray, AbstractFilter>(this, path);
    if(getErrorCode() >= 0)
    {
      dataArrayPaths.push_back(path);
      m_SelectedWeakPtrVector.push_back(ptr);
    }
  }

  getDataContainerArray()->validateNumberOfTuples<AbstractFilter>(this, dataArrayPaths);

  // Create a SufaceMesh Data Container with Faces, Vertices, Feature Labels and optionally Phase labels
  DataContainer::Pointer sm = getDataContainerArray()->createNonPrereqDataContainer<AbstractFilter>(this, getSurfaceDataContainerName(), DataContainerID);
  if(getErrorCode() < 0)
  {
    return;
  }

  QVector<size_t> tDims(1, 0);
  sm->createNonPrereqAttributeMatrix(this, getVertexAttributeMatrixName(), tDims, AttributeMatrix::Type::Vertex, AttributeMatrixID21);
  sm->createNonPrereqAttributeMatrix(this, getFaceAttributeMatrixName(), tDims, AttributeMatrix::Type::Face, AttributeMatrixID22);

  // Create a Triangle Geometry
  SharedVertexList::Pointer vertices = TriangleGeom::CreateSharedVertexList(0);
  TriangleGeom::Pointer triangleGeom = TriangleGeom::CreateGeometry(0, vertices, SIMPL::Geometry::TriangleGeometry, !getInPreflight());
  sm->setGeometry(triangleGeom);

  cDims[0] = 2;
  tempPath.update(getSurfaceDataContainerName().getDataContainerName(), getFaceAttributeMatrixName(), getFaceLabelsArrayName());
  m_FaceLabelsPtr = getDataContainerArray()->createNonPrereqArrayFromPath<DataArray<int32_t>, AbstractFilter>(this, tempPath, 0, cDims, "", DataArrayID31);
  if(nullptr != m_FaceLabelsPtr.lock()) /* Validate the Weak Pointer wraps a non-nullptr pointer to a DataArray<T> object */
  {
    m_FaceLabels = m_FaceLabelsPtr.lock()->getPointer(0);
  } /* Now assign the raw pointer to data from the DataArray<T> object */

  cDims[0] = 1;
  tempPath.update(getSurfaceDataContainerName().getDataContainerName(), getVertexAttributeMatrixName(), getNodeTypesArrayName());
  m_NodeTypesPtr = getDataContainerArray()->createNonPrereqArrayFromPath<DataArray<int8_t>, AbstractFilter>(this, tempPath, 0, cDims, "", DataArrayID32);
  if(nullptr != m_NodeTypesPtr.lock()) /* Validate the Weak Pointer wraps a non-nullptr pointer to a DataArray<T> object */
  {
    m_NodeTypes = m_NodeTypesPtr.lock()->getPointer(0);
  } /* Now assign the raw pointer to data from the DataArray<T> object */

  for(int32_t i = 0; i < m_SelectedWeakPtrVector.size(); i++)
  {
    tempPath.update(getSurfaceDataContainerName().getDataContainerName(), getFaceAttributeMatrixName(), m_SelectedDataArrayPaths[i].getDataArrayName());
    cDims = m_SelectedWeakPtrVector[i].lock()->getComponentDimensions();
    QVector<size_t> faceDims;
    // If the cell array is 1-dimensional, scale the face array accordingly
    if(cDims.size() == 1)
    {
      faceDims.push_back(cDims[0] * 2);
    }
    else
    {
      // If the cell array is multi-dimensional, error out for now because the Xdmf hyperslab output
      // will crash ParaView every time. This requires re-engineering the Xdmdf writing to support
      // the correct "standard" for writing "owners" of an array
      QString ss = QObject::tr("Selected Cell Attribute Arrays must have a single component dimension");
      setErrorCondition(-11005, ss);
    }
    m_CreatedWeakPtrVector.push_back(TemplateHelpers::CreateNonPrereqArrayFromArrayType()(this, tempPath, faceDims, m_SelectedWeakPtrVector[i].lock()));
  }

  if(m_SelectedWeakPtrVector.size() != m_CreatedWeakPtrVector.size())
  {
    QString ss = QObject::tr("The number of selected Cell Attribute Arrays available does not match the number of Face Attribute Arrays created");
    setErrorCondition(-11006, ss);
  }

  sm->createNonPrereqAttributeMatrix(this, getFeatureAttributeMatrixName(), tDims, AttributeMatrix::Type::FaceFeature, AttributeMatrixID23);

  // Create the TripleLines DataContainer
  getDataContainerArray()->createNonPrereqDataContainer<AbstractFilter>(this, getTripleLineDataContainerName(), DataContainerID);
}

// -----------------------------------------------------------------------------
//
// -----------------------------------------------------------------------------
void QuickSurfaceMesh::preflight()
{
  setInPreflight(true);
  emit preflightAboutToExecute();
  emit updateFilterParameters(this);
  dataCheck();
  emit preflightExecuted();
  setInPreflight(false);
}

// -----------------------------------------------------------------------------
//
// -----------------------------------------------------------------------------
void QuickSurfaceMesh::getGridCoordinates(const IGeometryGrid::Pointer& grid, size_t x, size_t y, size_t z, float* coords)
{
  float tmpCoords[3] = {0.0f, 0.0f, 0.0f};
  grid->getPlaneCoords(x, y, z, tmpCoords);
  coords[0] = tmpCoords[0];
  coords[1] = tmpCoords[1];
  coords[2] = tmpCoords[2];
}

// -----------------------------------------------------------------------------
//
// -----------------------------------------------------------------------------
void QuickSurfaceMesh::flipProblemVoxelCase1(int64_t v1, int64_t v2, int64_t v3, int64_t v4, int64_t v5, int64_t v6)
{
  SIMPL_RANDOMNG_NEW();

  float val = static_cast<float>(rg.genrand_res53());
  if(val < 0.25)
  {
    m_FeatureIds[v6] = m_FeatureIds[v4];
  }
  else if(val < 0.5)
  {
    m_FeatureIds[v6] = m_FeatureIds[v5];
  }
  else if(val < 0.75)
  {
    m_FeatureIds[v1] = m_FeatureIds[v2];
  }
  else
  {
    m_FeatureIds[v1] = m_FeatureIds[v3];
  }
}

// -----------------------------------------------------------------------------
//
// -----------------------------------------------------------------------------
void QuickSurfaceMesh::flipProblemVoxelCase2(int64_t v1, int64_t v2, int64_t v3, int64_t v4)
{
  SIMPL_RANDOMNG_NEW();

  float val = static_cast<float>(rg.genrand_res53());
  if(val < 0.125)
  {
    m_FeatureIds[v1] = m_FeatureIds[v2];
  }
  else if(val < 0.25)
  {
    m_FeatureIds[v1] = m_FeatureIds[v3];
  }
  else if(val < 0.375)
  {
    m_FeatureIds[v2] = m_FeatureIds[v1];
  }
  if(val < 0.5)
  {
    m_FeatureIds[v2] = m_FeatureIds[v4];
  }
  else if(val < 0.625)
  {
    m_FeatureIds[v3] = m_FeatureIds[v1];
  }
  else if(val < 0.75)
  {
    m_FeatureIds[v3] = m_FeatureIds[v4];
  }
  else if(val < 0.875)
  {
    m_FeatureIds[v4] = m_FeatureIds[v2];
  }
  else
  {
    m_FeatureIds[v4] = m_FeatureIds[v3];
  }
}

// -----------------------------------------------------------------------------
//
// -----------------------------------------------------------------------------
void QuickSurfaceMesh::flipProblemVoxelCase3(int64_t v1, int64_t v2, int64_t v3)
{
  SIMPL_RANDOMNG_NEW();

  float val = static_cast<float>(rg.genrand_res53());
  if(val < 0.5)
  {
    m_FeatureIds[v2] = m_FeatureIds[v1];
  }
  else
  {
    m_FeatureIds[v3] = m_FeatureIds[v1];
  }
}

// -----------------------------------------------------------------------------
//
// -----------------------------------------------------------------------------
void QuickSurfaceMesh::correctProblemVoxels()
{
  DataContainer::Pointer m = getDataContainerArray()->getDataContainer(m_FeatureIdsArrayPath.getDataContainerName());

  IGeometryGrid::Pointer grid = m->getGeometryAs<IGeometryGrid>();

  size_t udims[3] = {0, 0, 0};
  std::tie(udims[0], udims[1], udims[2]) = grid->getDimensions();

  int64_t dims[3] = {
      static_cast<int64_t>(udims[0]),
      static_cast<int64_t>(udims[1]),
      static_cast<int64_t>(udims[2])
  };

  int64_t xP = dims[0];
  int64_t yP = dims[1];
  int64_t zP = dims[2];

  int64_t v1 = 0, v2 = 0, v3 = 0, v4 = 0;
  int64_t v5 = 0, v6 = 0, v7 = 0, v8 = 0;

  int64_t f1 = 0, f2 = 0, f3 = 0, f4 = 0;
  int64_t f5 = 0, f6 = 0, f7 = 0, f8 = 0;

  int64_t row1, row2;
  int64_t plane1, plane2;

  int64_t count = 1;
  int64_t iter = 0;
  while(count > 0 && iter < 20)
  {
    iter++;
    count = 0;

    for(int64_t k = 1; k < zP; k++)
    {
      plane1 = (k - 1) * xP * yP;
      plane2 = k * xP * yP;
      for(int64_t j = 1; j < yP; j++)
      {
        row1 = (j - 1) * xP;
        row2 = j * xP;
        for(int64_t i = 1; i < xP; i++)
        {
          v1 = plane1 + row1 + i - 1;
          v2 = plane1 + row1 + i;
          v3 = plane1 + row2 + i - 1;
          v4 = plane1 + row2 + i;
          v5 = plane2 + row1 + i - 1;
          v6 = plane2 + row1 + i;
          v7 = plane2 + row2 + i - 1;
          v8 = plane2 + row2 + i;

          f1 = m_FeatureIds[v1];
          f2 = m_FeatureIds[v2];
          f3 = m_FeatureIds[v3];
          f4 = m_FeatureIds[v4];
          f5 = m_FeatureIds[v5];
          f6 = m_FeatureIds[v6];
          f7 = m_FeatureIds[v7];
          f8 = m_FeatureIds[v8];

          if(f1 == f8 && f1 != f2 && f1 != f3 && f1 != f4 && f1 != f5 && f1 != f6 && f1 != f7)
          {
            flipProblemVoxelCase1(v1, v2, v3, v6, v7, v8);
            count++;
          }
          if(f2 == f7 && f2 != f1 && f2 != f3 && f2 != f4 && f2 != f5 && f2 != f6 && f2 != f8)
          {
            flipProblemVoxelCase1(v2, v1, v4, v5, v8, v7);
            count++;
          }
          if(f3 == f6 && f3 != f1 && f3 != f2 && f3 != f4 && f3 != f5 && f3 != f7 && f3 != f8)
          {
            flipProblemVoxelCase1(v3, v1, v4, v5, v8, v6);
            count++;
          }
          if(f4 == f5 && f4 != f1 && f4 != f2 && f4 != f3 && f4 != f6 && f4 != f7 && f4 != f8)
          {
            flipProblemVoxelCase1(v4, v2, v3, v6, v7, v5);
            count++;
          }
          if(f1 == f6 && f1 != f2 && f1 != f5)
          {
            flipProblemVoxelCase2(v1, v2, v5, v6);
            count++;
          }
          if(f2 == f5 && f2 != f1 && f2 != f6)
          {
            flipProblemVoxelCase2(v2, v1, v6, v5);
            count++;
          }
          if(f3 == f8 && f3 != f4 && f3 != f7)
          {
            flipProblemVoxelCase2(v3, v4, v7, v8);
            count++;
          }
          if(f4 == f7 && f4 != f3 && f4 != f8)
          {
            flipProblemVoxelCase2(v4, v3, v8, v7);
            count++;
          }
          if(f1 == f7 && f1 != f3 && f1 != f5)
          {
            flipProblemVoxelCase2(v1, v3, v5, v7);
            count++;
          }
          if(f3 == f5 && f3 != f1 && f3 != f7)
          {
            flipProblemVoxelCase2(v3, v1, v7, v5);
            count++;
          }
          if(f2 == f8 && f2 != f4 && f2 != f6)
          {
            flipProblemVoxelCase2(v2, v4, v6, v8);
            count++;
          }
          if(f4 == f6 && f4 != f2 && f4 != f8)
          {
            flipProblemVoxelCase2(v4, v2, v8, v6);
            count++;
          }
          if(f1 == f4 && f1 != f2 && f1 != f3)
          {
            flipProblemVoxelCase2(v1, v2, v3, v4);
            count++;
          }
          if(f2 == f3 && f2 != f1 && f2 != f4)
          {
            flipProblemVoxelCase2(v2, v1, v4, v3);
            count++;
          }
          if(f5 == f8 && f5 != f6 && f5 != f7)
          {
            flipProblemVoxelCase2(v5, v6, v7, v8);
            count++;
          }
          if(f6 == f7 && f6 != f5 && f6 != f8)
          {
            flipProblemVoxelCase2(v6, v5, v8, v7);
            count++;
          }
          if(f2 == f3 && f2 == f4 && f2 == f5 && f2 == f6 && f2 == f7 && f2 != f1 && f2 != f8)
          {
            flipProblemVoxelCase3(v2, v1, v8);
            count++;
          }
          if(f1 == f3 && f1 == f4 && f1 == f5 && f1 == f7 && f2 == f8 && f1 != f2 && f1 != f7)
          {
            flipProblemVoxelCase3(v1, v2, v7);
            count++;
          }
          if(f1 == f2 && f1 == f4 && f1 == f5 && f1 == f7 && f1 == f8 && f1 != f3 && f1 != f6)
          {
            flipProblemVoxelCase3(v1, v3, v6);
            count++;
          }
          if(f1 == f2 && f1 == f3 && f1 == f6 && f1 == f7 && f1 == f8 && f1 != f4 && f1 != f5)
          {
            flipProblemVoxelCase3(v1, v4, v5);
            count++;
          }
        }
      }
    }
    QString ss = QObject::tr("Correcting Problem Voxels: Iteration - '%1'; Problem Voxels - '%2'").arg(iter).arg(count);
    notifyStatusMessage(ss);
  }
}

// -----------------------------------------------------------------------------
//
// -----------------------------------------------------------------------------
void QuickSurfaceMesh::determineActiveNodes(std::vector<int64_t>& m_NodeIds, int64_t& nodeCount, int64_t& triangleCount)
{
  DataContainer::Pointer m = getDataContainerArray()->getDataContainer(m_FeatureIdsArrayPath.getDataContainerName());

  IGeometryGrid::Pointer grid = m->getGeometryAs<IGeometryGrid>();

  size_t udims[3] = {0, 0, 0};
  std::tie(udims[0], udims[1], udims[2]) = grid->getDimensions();

  int64_t dims[3] = {
      static_cast<int64_t>(udims[0]),
      static_cast<int64_t>(udims[1]),
      static_cast<int64_t>(udims[2]),
  };

  int64_t xP = dims[0];
  int64_t yP = dims[1];
  int64_t zP = dims[2];

  std::vector<std::set<int32_t>> ownerLists;

  int64_t point = 0, neigh1 = 0, neigh2 = 0, neigh3 = 0;

  int64_t nodeId1 = 0, nodeId2 = 0, nodeId3 = 0, nodeId4 = 0;

  // first determining which nodes are actually boundary nodes and
  // count number of nodes and triangles that will be created
  for(int64_t k = 0; k < zP; k++)
  {
    for(int64_t j = 0; j < yP; j++)
    {
      for(int64_t i = 0; i < xP; i++)
      {
        point = (k * xP * yP) + (j * xP) + i;
        neigh1 = point + 1;
        neigh2 = point + xP;
        neigh3 = point + (xP * yP);

        if(i == 0)
        {
          nodeId1 = (k * (xP + 1) * (yP + 1)) + (j * (xP + 1)) + i;
          if(m_NodeIds[nodeId1] == -1)
          {
            m_NodeIds[nodeId1] = nodeCount;
            nodeCount++;
          }
          nodeId2 = (k * (xP + 1) * (yP + 1)) + ((j + 1) * (xP + 1)) + i;
          if(m_NodeIds[nodeId2] == -1)
          {
            m_NodeIds[nodeId2] = nodeCount;
            nodeCount++;
          }
          nodeId3 = ((k + 1) * (xP + 1) * (yP + 1)) + (j * (xP + 1)) + i;
          if(m_NodeIds[nodeId3] == -1)
          {
            m_NodeIds[nodeId3] = nodeCount;
            nodeCount++;
          }
          nodeId4 = ((k + 1) * (xP + 1) * (yP + 1)) + ((j + 1) * (xP + 1)) + i;
          if(m_NodeIds[nodeId4] == -1)
          {
            m_NodeIds[nodeId4] = nodeCount;
            nodeCount++;
          }
          triangleCount++;
          triangleCount++;
        }
        if(j == 0)
        {
          nodeId1 = (k * (xP + 1) * (yP + 1)) + (j * (xP + 1)) + i;
          if(m_NodeIds[nodeId1] == -1)
          {
            m_NodeIds[nodeId1] = nodeCount;
            nodeCount++;
          }
          nodeId2 = (k * (xP + 1) * (yP + 1)) + (j * (xP + 1)) + (i + 1);
          if(m_NodeIds[nodeId2] == -1)
          {
            m_NodeIds[nodeId2] = nodeCount;
            nodeCount++;
          }
          nodeId3 = ((k + 1) * (xP + 1) * (yP + 1)) + (j * (xP + 1)) + i;
          if(m_NodeIds[nodeId3] == -1)
          {
            m_NodeIds[nodeId3] = nodeCount;
            nodeCount++;
          }
          nodeId4 = ((k + 1) * (xP + 1) * (yP + 1)) + (j * (xP + 1)) + (i + 1);
          if(m_NodeIds[nodeId4] == -1)
          {
            m_NodeIds[nodeId4] = nodeCount;
            nodeCount++;
          }
          triangleCount++;
          triangleCount++;
        }
        if(k == 0)
        {
          nodeId1 = (k * (xP + 1) * (yP + 1)) + (j * (xP + 1)) + i;
          if(m_NodeIds[nodeId1] == -1)
          {
            m_NodeIds[nodeId1] = nodeCount;
            nodeCount++;
          }
          nodeId2 = (k * (xP + 1) * (yP + 1)) + (j * (xP + 1)) + (i + 1);
          if(m_NodeIds[nodeId2] == -1)
          {
            m_NodeIds[nodeId2] = nodeCount;
            nodeCount++;
          }
          nodeId3 = (k * (xP + 1) * (yP + 1)) + ((j + 1) * (xP + 1)) + i;
          if(m_NodeIds[nodeId3] == -1)
          {
            m_NodeIds[nodeId3] = nodeCount;
            nodeCount++;
          }
          nodeId4 = (k * (xP + 1) * (yP + 1)) + ((j + 1) * (xP + 1)) + (i + 1);
          if(m_NodeIds[nodeId4] == -1)
          {
            m_NodeIds[nodeId4] = nodeCount;
            nodeCount++;
          }
          triangleCount++;
          triangleCount++;
        }
        if(i == (xP - 1))
        {
          nodeId1 = (k * (xP + 1) * (yP + 1)) + (j * (xP + 1)) + (i + 1);
          if(m_NodeIds[nodeId1] == -1)
          {
            m_NodeIds[nodeId1] = nodeCount;
            nodeCount++;
          }
          nodeId2 = (k * (xP + 1) * (yP + 1)) + ((j + 1) * (xP + 1)) + (i + 1);
          if(m_NodeIds[nodeId2] == -1)
          {
            m_NodeIds[nodeId2] = nodeCount;
            nodeCount++;
          }
          nodeId3 = ((k + 1) * (xP + 1) * (yP + 1)) + (j * (xP + 1)) + (i + 1);
          if(m_NodeIds[nodeId3] == -1)
          {
            m_NodeIds[nodeId3] = nodeCount;
            nodeCount++;
          }
          nodeId4 = ((k + 1) * (xP + 1) * (yP + 1)) + ((j + 1) * (xP + 1)) + (i + 1);
          if(m_NodeIds[nodeId4] == -1)
          {
            m_NodeIds[nodeId4] = nodeCount;
            nodeCount++;
          }
          triangleCount++;
          triangleCount++;
        }
        else if(m_FeatureIds[point] != m_FeatureIds[neigh1])
        {
          nodeId1 = (k * (xP + 1) * (yP + 1)) + (j * (xP + 1)) + (i + 1);
          if(m_NodeIds[nodeId1] == -1)
          {
            m_NodeIds[nodeId1] = nodeCount;
            nodeCount++;
          }
          nodeId2 = (k * (xP + 1) * (yP + 1)) + ((j + 1) * (xP + 1)) + (i + 1);
          if(m_NodeIds[nodeId2] == -1)
          {
            m_NodeIds[nodeId2] = nodeCount;
            nodeCount++;
          }
          nodeId3 = ((k + 1) * (xP + 1) * (yP + 1)) + (j * (xP + 1)) + (i + 1);
          if(m_NodeIds[nodeId3] == -1)
          {
            m_NodeIds[nodeId3] = nodeCount;
            nodeCount++;
          }
          nodeId4 = ((k + 1) * (xP + 1) * (yP + 1)) + ((j + 1) * (xP + 1)) + (i + 1);
          if(m_NodeIds[nodeId4] == -1)
          {
            m_NodeIds[nodeId4] = nodeCount;
            nodeCount++;
          }
          triangleCount++;
          triangleCount++;
        }
        if(j == (yP - 1))
        {
          nodeId1 = (k * (xP + 1) * (yP + 1)) + ((j + 1) * (xP + 1)) + (i + 1);
          if(m_NodeIds[nodeId1] == -1)
          {
            m_NodeIds[nodeId1] = nodeCount;
            nodeCount++;
          }
          nodeId2 = (k * (xP + 1) * (yP + 1)) + ((j + 1) * (xP + 1)) + i;
          if(m_NodeIds[nodeId2] == -1)
          {
            m_NodeIds[nodeId2] = nodeCount;
            nodeCount++;
          }
          nodeId3 = ((k + 1) * (xP + 1) * (yP + 1)) + ((j + 1) * (xP + 1)) + (i + 1);
          if(m_NodeIds[nodeId3] == -1)
          {
            m_NodeIds[nodeId3] = nodeCount;
            nodeCount++;
          }
          nodeId4 = ((k + 1) * (xP + 1) * (yP + 1)) + ((j + 1) * (xP + 1)) + i;
          if(m_NodeIds[nodeId4] == -1)
          {
            m_NodeIds[nodeId4] = nodeCount;
            nodeCount++;
          }
          triangleCount++;
          triangleCount++;
        }
        else if(m_FeatureIds[point] != m_FeatureIds[neigh2])
        {
          nodeId1 = (k * (xP + 1) * (yP + 1)) + ((j + 1) * (xP + 1)) + (i + 1);
          if(m_NodeIds[nodeId1] == -1)
          {
            m_NodeIds[nodeId1] = nodeCount;
            nodeCount++;
          }
          nodeId2 = (k * (xP + 1) * (yP + 1)) + ((j + 1) * (xP + 1)) + i;
          if(m_NodeIds[nodeId2] == -1)
          {
            m_NodeIds[nodeId2] = nodeCount;
            nodeCount++;
          }
          nodeId3 = ((k + 1) * (xP + 1) * (yP + 1)) + ((j + 1) * (xP + 1)) + (i + 1);
          if(m_NodeIds[nodeId3] == -1)
          {
            m_NodeIds[nodeId3] = nodeCount;
            nodeCount++;
          }
          nodeId4 = ((k + 1) * (xP + 1) * (yP + 1)) + ((j + 1) * (xP + 1)) + i;
          if(m_NodeIds[nodeId4] == -1)
          {
            m_NodeIds[nodeId4] = nodeCount;
            nodeCount++;
          }
          triangleCount++;
          triangleCount++;
        }
        if(k == (zP - 1))
        {
          nodeId1 = ((k + 1) * (xP + 1) * (yP + 1)) + (j * (xP + 1)) + (i + 1);
          if(m_NodeIds[nodeId1] == -1)
          {
            m_NodeIds[nodeId1] = nodeCount;
            nodeCount++;
          }
          nodeId2 = ((k + 1) * (xP + 1) * (yP + 1)) + (j * (xP + 1)) + i;
          if(m_NodeIds[nodeId2] == -1)
          {
            m_NodeIds[nodeId2] = nodeCount;
            nodeCount++;
          }
          nodeId3 = ((k + 1) * (xP + 1) * (yP + 1)) + ((j + 1) * (xP + 1)) + (i + 1);
          if(m_NodeIds[nodeId3] == -1)
          {
            m_NodeIds[nodeId3] = nodeCount;
            nodeCount++;
          }
          nodeId4 = ((k + 1) * (xP + 1) * (yP + 1)) + ((j + 1) * (xP + 1)) + i;
          if(m_NodeIds[nodeId4] == -1)
          {
            m_NodeIds[nodeId4] = nodeCount;
            nodeCount++;
          }
          triangleCount++;
          triangleCount++;
        }
        else if(k < zP - 1 && m_FeatureIds[point] != m_FeatureIds[neigh3])
        {
          nodeId1 = ((k + 1) * (xP + 1) * (yP + 1)) + (j * (xP + 1)) + (i + 1);
          if(m_NodeIds[nodeId1] == -1)
          {
            m_NodeIds[nodeId1] = nodeCount;
            nodeCount++;
          }
          nodeId2 = ((k + 1) * (xP + 1) * (yP + 1)) + (j * (xP + 1)) + i;
          if(m_NodeIds[nodeId2] == -1)
          {
            m_NodeIds[nodeId2] = nodeCount;
            nodeCount++;
          }
          nodeId3 = ((k + 1) * (xP + 1) * (yP + 1)) + ((j + 1) * (xP + 1)) + (i + 1);
          if(m_NodeIds[nodeId3] == -1)
          {
            m_NodeIds[nodeId3] = nodeCount;
            nodeCount++;
          }
          nodeId4 = ((k + 1) * (xP + 1) * (yP + 1)) + ((j + 1) * (xP + 1)) + i;
          if(m_NodeIds[nodeId4] == -1)
          {
            m_NodeIds[nodeId4] = nodeCount;
            nodeCount++;
          }
          triangleCount++;
          triangleCount++;
        }
      }
    }
  }
}

// -----------------------------------------------------------------------------
//
// -----------------------------------------------------------------------------
void QuickSurfaceMesh::createNodesAndTriangles(std::vector<int64_t> m_NodeIds, int64_t nodeCount, int64_t triangleCount)
{
  DataContainer::Pointer m = getDataContainerArray()->getDataContainer(m_FeatureIdsArrayPath.getDataContainerName());
  DataContainer::Pointer sm = getDataContainerArray()->getDataContainer(getSurfaceDataContainerName());

  AttributeMatrix::Pointer featAttrMat = sm->getAttributeMatrix(m_FeatureAttributeMatrixName);
  size_t numFeatures = 0;
  size_t numTuples = m_FeatureIdsPtr.lock()->getNumberOfTuples();
  for(size_t i = 0; i < numTuples; i++)
  {
    if(m_FeatureIds[i] > numFeatures)
    {
      numFeatures = m_FeatureIds[i];
    }
  }

  QVector<size_t> featDims(1, numFeatures + 1);
  featAttrMat->setTupleDimensions(featDims);

  IGeometryGrid::Pointer grid = m->getGeometryAs<IGeometryGrid>();

  size_t udims[3] = {0, 0, 0};
  std::tie(udims[0], udims[1], udims[2]) = grid->getDimensions();

  int64_t dims[3] = {
      static_cast<int64_t>(udims[0]),
      static_cast<int64_t>(udims[1]),
      static_cast<int64_t>(udims[2]),
  };

  int64_t xP = dims[0];
  int64_t yP = dims[1];
  int64_t zP = dims[2];

  std::vector<std::set<int32_t>> ownerLists;

  int64_t point = 0, neigh1 = 0, neigh2 = 0, neigh3 = 0;

  int64_t nodeId1 = 0, nodeId2 = 0, nodeId3 = 0, nodeId4 = 0;

  int64_t cIndex1 = 0, cIndex2 = 0;

  TriangleGeom::Pointer triangleGeom = sm->getGeometryAs<TriangleGeom>();

  float* vertex = triangleGeom->getVertexPointer(0);
  int64_t* triangle = triangleGeom->getTriPointer(0);

  QVector<size_t> tDims(1, nodeCount);
  sm->getAttributeMatrix(getVertexAttributeMatrixName())->resizeAttributeArrays(tDims);
  tDims[0] = triangleCount;
  sm->getAttributeMatrix(getFaceAttributeMatrixName())->resizeAttributeArrays(tDims);

  updateVertexInstancePointers();
  updateFaceInstancePointers();

  ownerLists.resize(nodeCount);

  // Cycle through again assigning coordinates to each node and assigning node numbers and feature labels to each triangle
  int64_t triangleIndex = 0;
  for(int64_t k = 0; k < zP; k++)
  {
    for(int64_t j = 0; j < yP; j++)
    {
      for(int64_t i = 0; i < xP; i++)
      {
        point = (k * xP * yP) + (j * xP) + i;
        neigh1 = point + 1; // <== What happens if we are at the end of a row?
        neigh2 = point + xP;
        neigh3 = point + (xP * yP);

        if(i == 0)
        {
          nodeId1 = (k * (xP + 1) * (yP + 1)) + (j * (xP + 1)) + i;
          getGridCoordinates(grid, i, j, k, vertex + (m_NodeIds[nodeId1] * 3));

          nodeId2 = (k * (xP + 1) * (yP + 1)) + ((j + 1) * (xP + 1)) + i;
          getGridCoordinates(grid, i, j + 1, k, vertex + (m_NodeIds[nodeId2] * 3));

          nodeId3 = ((k + 1) * (xP + 1) * (yP + 1)) + (j * (xP + 1)) + i;
          getGridCoordinates(grid, i, j, k + 1, vertex + (m_NodeIds[nodeId3] * 3));

          nodeId4 = ((k + 1) * (xP + 1) * (yP + 1)) + ((j + 1) * (xP + 1)) + i;
          getGridCoordinates(grid, i + 1, j + 1, k + 1, vertex + (m_NodeIds[nodeId4] * 3));

          triangle[triangleIndex * 3 + 0] = m_NodeIds[nodeId1];
          triangle[triangleIndex * 3 + 1] = m_NodeIds[nodeId3];
          triangle[triangleIndex * 3 + 2] = m_NodeIds[nodeId2];
          m_FaceLabels[triangleIndex * 2] = -1;
          m_FaceLabels[triangleIndex * 2 + 1] = m_FeatureIds[point];

          for(int32_t i = 0; i < m_SelectedWeakPtrVector.size(); i++)
          {
            EXECUTE_FUNCTION_TEMPLATE(this, copyCellArraysToFaceArrays, m_SelectedWeakPtrVector[i].lock(), triangleIndex, point, point, m_SelectedWeakPtrVector[i].lock(),
                                      m_CreatedWeakPtrVector[i].lock(), true)
          }

          triangleIndex++;

          triangle[triangleIndex * 3 + 0] = m_NodeIds[nodeId2];
          triangle[triangleIndex * 3 + 1] = m_NodeIds[nodeId3];
          triangle[triangleIndex * 3 + 2] = m_NodeIds[nodeId4];
          m_FaceLabels[triangleIndex * 2] = -1;
          m_FaceLabels[triangleIndex * 2 + 1] = m_FeatureIds[point];

          for(int32_t i = 0; i < m_SelectedWeakPtrVector.size(); i++)
          {
            EXECUTE_FUNCTION_TEMPLATE(this, copyCellArraysToFaceArrays, m_SelectedWeakPtrVector[i].lock(), triangleIndex, point, point, m_SelectedWeakPtrVector[i].lock(),
                                      m_CreatedWeakPtrVector[i].lock(), true)
          }

          triangleIndex++;

          ownerLists[m_NodeIds[nodeId1]].insert(m_FeatureIds[point]);
          ownerLists[m_NodeIds[nodeId1]].insert(-1);
          ownerLists[m_NodeIds[nodeId2]].insert(m_FeatureIds[point]);
          ownerLists[m_NodeIds[nodeId2]].insert(-1);
          ownerLists[m_NodeIds[nodeId3]].insert(m_FeatureIds[point]);
          ownerLists[m_NodeIds[nodeId3]].insert(-1);
          ownerLists[m_NodeIds[nodeId4]].insert(m_FeatureIds[point]);
          ownerLists[m_NodeIds[nodeId4]].insert(-1);
        }
        if(j == 0)
        {
          nodeId1 = (k * (xP + 1) * (yP + 1)) + (j * (xP + 1)) + i;
          getGridCoordinates(grid, i, j, k, vertex + (m_NodeIds[nodeId1] * 3));

          nodeId2 = (k * (xP + 1) * (yP + 1)) + (j * (xP + 1)) + (i + 1);
          getGridCoordinates(grid, i + 1, j, k, vertex + (m_NodeIds[nodeId2] * 3));

          nodeId3 = ((k + 1) * (xP + 1) * (yP + 1)) + (j * (xP + 1)) + i;
          getGridCoordinates(grid, i, j, k + 1, vertex + (m_NodeIds[nodeId3] * 3));

          nodeId4 = ((k + 1) * (xP + 1) * (yP + 1)) + (j * (xP + 1)) + (i + 1);
          getGridCoordinates(grid, i + 1, j, k + 1, vertex + (m_NodeIds[nodeId4] * 3));

          triangle[triangleIndex * 3 + 0] = m_NodeIds[nodeId1];
          triangle[triangleIndex * 3 + 1] = m_NodeIds[nodeId2];
          triangle[triangleIndex * 3 + 2] = m_NodeIds[nodeId3];
          m_FaceLabels[triangleIndex * 2] = -1;
          m_FaceLabels[triangleIndex * 2 + 1] = m_FeatureIds[point];

          for(int32_t i = 0; i < m_SelectedWeakPtrVector.size(); i++)
          {
            EXECUTE_FUNCTION_TEMPLATE(this, copyCellArraysToFaceArrays, m_SelectedWeakPtrVector[i].lock(), triangleIndex, point, point, m_SelectedWeakPtrVector[i].lock(),
                                      m_CreatedWeakPtrVector[i].lock(), true)
          }

          triangleIndex++;

          triangle[triangleIndex * 3 + 0] = m_NodeIds[nodeId2];
          triangle[triangleIndex * 3 + 1] = m_NodeIds[nodeId4];
          triangle[triangleIndex * 3 + 2] = m_NodeIds[nodeId3];
          m_FaceLabels[triangleIndex * 2] = -1;
          m_FaceLabels[triangleIndex * 2 + 1] = m_FeatureIds[point];

          for(int32_t i = 0; i < m_SelectedWeakPtrVector.size(); i++)
          {
            EXECUTE_FUNCTION_TEMPLATE(this, copyCellArraysToFaceArrays, m_SelectedWeakPtrVector[i].lock(), triangleIndex, point, point, m_SelectedWeakPtrVector[i].lock(),
                                      m_CreatedWeakPtrVector[i].lock(), true)
          }

          triangleIndex++;

          ownerLists[m_NodeIds[nodeId1]].insert(m_FeatureIds[point]);
          ownerLists[m_NodeIds[nodeId1]].insert(-1);
          ownerLists[m_NodeIds[nodeId2]].insert(m_FeatureIds[point]);
          ownerLists[m_NodeIds[nodeId2]].insert(-1);
          ownerLists[m_NodeIds[nodeId3]].insert(m_FeatureIds[point]);
          ownerLists[m_NodeIds[nodeId3]].insert(-1);
          ownerLists[m_NodeIds[nodeId4]].insert(m_FeatureIds[point]);
          ownerLists[m_NodeIds[nodeId4]].insert(-1);
        }
        if(k == 0)
        {
          nodeId1 = (k * (xP + 1) * (yP + 1)) + (j * (xP + 1)) + i;
          getGridCoordinates(grid, i, j, k, vertex + (m_NodeIds[nodeId1] * 3));

          nodeId2 = (k * (xP + 1) * (yP + 1)) + (j * (xP + 1)) + (i + 1);
          getGridCoordinates(grid, i + 1, j, k, vertex + (m_NodeIds[nodeId2] * 3));

          nodeId3 = (k * (xP + 1) * (yP + 1)) + ((j + 1) * (xP + 1)) + i;
          getGridCoordinates(grid, i, j + 1, k, vertex + (m_NodeIds[nodeId3] * 3));

          nodeId4 = (k * (xP + 1) * (yP + 1)) + ((j + 1) * (xP + 1)) + (i + 1);
          getGridCoordinates(grid, i + 1, j + 1, k, vertex + (m_NodeIds[nodeId4] * 3));

          triangle[triangleIndex * 3 + 0] = m_NodeIds[nodeId1];
          triangle[triangleIndex * 3 + 1] = m_NodeIds[nodeId3];
          triangle[triangleIndex * 3 + 2] = m_NodeIds[nodeId2];
          m_FaceLabels[triangleIndex * 2] = -1;
          m_FaceLabels[triangleIndex * 2 + 1] = m_FeatureIds[point];

          for(int32_t i = 0; i < m_SelectedWeakPtrVector.size(); i++)
          {
            EXECUTE_FUNCTION_TEMPLATE(this, copyCellArraysToFaceArrays, m_SelectedWeakPtrVector[i].lock(), triangleIndex, point, point, m_SelectedWeakPtrVector[i].lock(),
                                      m_CreatedWeakPtrVector[i].lock(), true)
          }

          triangleIndex++;

          triangle[triangleIndex * 3 + 0] = m_NodeIds[nodeId2];
          triangle[triangleIndex * 3 + 1] = m_NodeIds[nodeId3];
          triangle[triangleIndex * 3 + 2] = m_NodeIds[nodeId4];
          m_FaceLabels[triangleIndex * 2] = -1;
          m_FaceLabels[triangleIndex * 2 + 1] = m_FeatureIds[point];

          for(int32_t i = 0; i < m_SelectedWeakPtrVector.size(); i++)
          {
            EXECUTE_FUNCTION_TEMPLATE(this, copyCellArraysToFaceArrays, m_SelectedWeakPtrVector[i].lock(), triangleIndex, point, point, m_SelectedWeakPtrVector[i].lock(),
                                      m_CreatedWeakPtrVector[i].lock(), true)
          }

          triangleIndex++;

          ownerLists[m_NodeIds[nodeId1]].insert(m_FeatureIds[point]);
          ownerLists[m_NodeIds[nodeId1]].insert(-1);
          ownerLists[m_NodeIds[nodeId2]].insert(m_FeatureIds[point]);
          ownerLists[m_NodeIds[nodeId2]].insert(-1);
          ownerLists[m_NodeIds[nodeId3]].insert(m_FeatureIds[point]);
          ownerLists[m_NodeIds[nodeId3]].insert(-1);
          ownerLists[m_NodeIds[nodeId4]].insert(m_FeatureIds[point]);
          ownerLists[m_NodeIds[nodeId4]].insert(-1);
        }
        if(i == (xP - 1)) // Takes care of the end of a Row...
        {
          nodeId1 = (k * (xP + 1) * (yP + 1)) + (j * (xP + 1)) + (i + 1);
          getGridCoordinates(grid, i + 1, j, k, vertex + (m_NodeIds[nodeId1] * 3));

          nodeId2 = (k * (xP + 1) * (yP + 1)) + ((j + 1) * (xP + 1)) + (i + 1);
          getGridCoordinates(grid, i + 1, j + 1, k, vertex + (m_NodeIds[nodeId2] * 3));

          nodeId3 = ((k + 1) * (xP + 1) * (yP + 1)) + (j * (xP + 1)) + (i + 1);
          getGridCoordinates(grid, i + 1, j, k + 1, vertex + (m_NodeIds[nodeId3] * 3));

          nodeId4 = ((k + 1) * (xP + 1) * (yP + 1)) + ((j + 1) * (xP + 1)) + (i + 1);
          getGridCoordinates(grid, i + 1, j + 1, k + 1, vertex + (m_NodeIds[nodeId4] * 3));

          triangle[triangleIndex * 3 + 0] = m_NodeIds[nodeId1];
          triangle[triangleIndex * 3 + 1] = m_NodeIds[nodeId2];
          triangle[triangleIndex * 3 + 2] = m_NodeIds[nodeId3];
          m_FaceLabels[triangleIndex * 2] = -1;
          m_FaceLabels[triangleIndex * 2 + 1] = m_FeatureIds[point];

          for(int32_t i = 0; i < m_SelectedWeakPtrVector.size(); i++)
          {
            EXECUTE_FUNCTION_TEMPLATE(this, copyCellArraysToFaceArrays, m_SelectedWeakPtrVector[i].lock(), triangleIndex, point, point, m_SelectedWeakPtrVector[i].lock(),
                                      m_CreatedWeakPtrVector[i].lock(), true)
          }

          triangleIndex++;

          triangle[triangleIndex * 3 + 0] = m_NodeIds[nodeId2];
          triangle[triangleIndex * 3 + 1] = m_NodeIds[nodeId4];
          triangle[triangleIndex * 3 + 2] = m_NodeIds[nodeId3];
          m_FaceLabels[triangleIndex * 2] = -1;
          m_FaceLabels[triangleIndex * 2 + 1] = m_FeatureIds[point];

          for(int32_t i = 0; i < m_SelectedWeakPtrVector.size(); i++)
          {
            EXECUTE_FUNCTION_TEMPLATE(this, copyCellArraysToFaceArrays, m_SelectedWeakPtrVector[i].lock(), triangleIndex, point, point, m_SelectedWeakPtrVector[i].lock(),
                                      m_CreatedWeakPtrVector[i].lock(), true)
          }

          triangleIndex++;

          ownerLists[m_NodeIds[nodeId1]].insert(m_FeatureIds[point]);
          ownerLists[m_NodeIds[nodeId1]].insert(-1);
          ownerLists[m_NodeIds[nodeId2]].insert(m_FeatureIds[point]);
          ownerLists[m_NodeIds[nodeId2]].insert(-1);
          ownerLists[m_NodeIds[nodeId3]].insert(m_FeatureIds[point]);
          ownerLists[m_NodeIds[nodeId3]].insert(-1);
          ownerLists[m_NodeIds[nodeId4]].insert(m_FeatureIds[point]);
          ownerLists[m_NodeIds[nodeId4]].insert(-1);
        }
        else if(m_FeatureIds[point] != m_FeatureIds[neigh1])
        {
          nodeId1 = (k * (xP + 1) * (yP + 1)) + (j * (xP + 1)) + (i + 1);
          getGridCoordinates(grid, i + 1, j, k, vertex + (m_NodeIds[nodeId1] * 3));

          nodeId2 = (k * (xP + 1) * (yP + 1)) + ((j + 1) * (xP + 1)) + (i + 1);
          getGridCoordinates(grid, i + 1, j + 1, k, vertex + (m_NodeIds[nodeId2] * 3));

          nodeId3 = ((k + 1) * (xP + 1) * (yP + 1)) + (j * (xP + 1)) + (i + 1);
          getGridCoordinates(grid, i + 1, j, k + 1, vertex + (m_NodeIds[nodeId3] * 3));

          nodeId4 = ((k + 1) * (xP + 1) * (yP + 1)) + ((j + 1) * (xP + 1)) + (i + 1);
          getGridCoordinates(grid, i + 1, j + 1, k + 1, vertex + (m_NodeIds[nodeId4] * 3));

          triangle[triangleIndex * 3 + 0] = m_NodeIds[nodeId1];
          triangle[triangleIndex * 3 + 1] = m_NodeIds[nodeId2];
          triangle[triangleIndex * 3 + 2] = m_NodeIds[nodeId3];
          m_FaceLabels[triangleIndex * 2] = m_FeatureIds[neigh1];
          m_FaceLabels[triangleIndex * 2 + 1] = m_FeatureIds[point];
          cIndex1 = neigh1;
          cIndex2 = point;
          if(m_FeatureIds[point] < m_FeatureIds[neigh1])
          {
            triangle[triangleIndex * 3 + 1] = m_NodeIds[nodeId3];
            triangle[triangleIndex * 3 + 2] = m_NodeIds[nodeId2];
            m_FaceLabels[triangleIndex * 2] = m_FeatureIds[point];
            m_FaceLabels[triangleIndex * 2 + 1] = m_FeatureIds[neigh1];
            cIndex1 = point;
            cIndex2 = neigh1;
          }

          for(int32_t i = 0; i < m_SelectedWeakPtrVector.size(); i++)
          {
            EXECUTE_FUNCTION_TEMPLATE(this, copyCellArraysToFaceArrays, m_SelectedWeakPtrVector[i].lock(), triangleIndex, neigh1, point, m_SelectedWeakPtrVector[i].lock(),
                                      m_CreatedWeakPtrVector[i].lock())
          }

          triangleIndex++;

          triangle[triangleIndex * 3 + 0] = m_NodeIds[nodeId2];
          triangle[triangleIndex * 3 + 1] = m_NodeIds[nodeId4];
          triangle[triangleIndex * 3 + 2] = m_NodeIds[nodeId3];
          m_FaceLabels[triangleIndex * 2] = m_FeatureIds[neigh1];
          m_FaceLabels[triangleIndex * 2 + 1] = m_FeatureIds[point];
          cIndex1 = neigh1;
          cIndex2 = point;
          if(m_FeatureIds[point] < m_FeatureIds[neigh1])
          {
            triangle[triangleIndex * 3 + 1] = m_NodeIds[nodeId3];
            triangle[triangleIndex * 3 + 2] = m_NodeIds[nodeId4];
            m_FaceLabels[triangleIndex * 2] = m_FeatureIds[point];
            m_FaceLabels[triangleIndex * 2 + 1] = m_FeatureIds[neigh1];
            cIndex1 = point;
            cIndex2 = neigh1;
          }

          for(int32_t i = 0; i < m_SelectedWeakPtrVector.size(); i++)
          {
            EXECUTE_FUNCTION_TEMPLATE(this, copyCellArraysToFaceArrays, m_SelectedWeakPtrVector[i].lock(), triangleIndex, neigh1, point, m_SelectedWeakPtrVector[i].lock(),
                                      m_CreatedWeakPtrVector[i].lock())
          }

          triangleIndex++;

          ownerLists[m_NodeIds[nodeId1]].insert(m_FeatureIds[point]);
          ownerLists[m_NodeIds[nodeId1]].insert(m_FeatureIds[neigh1]);
          ownerLists[m_NodeIds[nodeId2]].insert(m_FeatureIds[point]);
          ownerLists[m_NodeIds[nodeId2]].insert(m_FeatureIds[neigh1]);
          ownerLists[m_NodeIds[nodeId3]].insert(m_FeatureIds[point]);
          ownerLists[m_NodeIds[nodeId3]].insert(m_FeatureIds[neigh1]);
          ownerLists[m_NodeIds[nodeId4]].insert(m_FeatureIds[point]);
          ownerLists[m_NodeIds[nodeId4]].insert(m_FeatureIds[neigh1]);
        }
        if(j == (yP - 1)) // Takes care of the end of a column
        {
          nodeId1 = (k * (xP + 1) * (yP + 1)) + ((j + 1) * (xP + 1)) + (i + 1);
          getGridCoordinates(grid, i + 1, j + 1, k, vertex + (m_NodeIds[nodeId1] * 3));

          nodeId2 = (k * (xP + 1) * (yP + 1)) + ((j + 1) * (xP + 1)) + i;
          getGridCoordinates(grid, i, j + 1, k, vertex + (m_NodeIds[nodeId2] * 3));

          nodeId3 = ((k + 1) * (xP + 1) * (yP + 1)) + ((j + 1) * (xP + 1)) + (i + 1);
          getGridCoordinates(grid, i + 1, j + 1, k + 1, vertex + (m_NodeIds[nodeId3] * 3));

          nodeId4 = ((k + 1) * (xP + 1) * (yP + 1)) + ((j + 1) * (xP + 1)) + i;
          getGridCoordinates(grid, i, j + 1, k + 1, vertex + (m_NodeIds[nodeId4] * 3));

          triangle[triangleIndex * 3 + 0] = m_NodeIds[nodeId1];
          triangle[triangleIndex * 3 + 1] = m_NodeIds[nodeId2];
          triangle[triangleIndex * 3 + 2] = m_NodeIds[nodeId3];
          m_FaceLabels[triangleIndex * 2] = -1;
          m_FaceLabels[triangleIndex * 2 + 1] = m_FeatureIds[point];

          for(int32_t i = 0; i < m_SelectedWeakPtrVector.size(); i++)
          {
            EXECUTE_FUNCTION_TEMPLATE(this, copyCellArraysToFaceArrays, m_SelectedWeakPtrVector[i].lock(), triangleIndex, point, point, m_SelectedWeakPtrVector[i].lock(),
                                      m_CreatedWeakPtrVector[i].lock(), true)
          }

          triangleIndex++;

          triangle[triangleIndex * 3 + 0] = m_NodeIds[nodeId2];
          triangle[triangleIndex * 3 + 1] = m_NodeIds[nodeId4];
          triangle[triangleIndex * 3 + 2] = m_NodeIds[nodeId3];
          m_FaceLabels[triangleIndex * 2] = -1;
          m_FaceLabels[triangleIndex * 2 + 1] = m_FeatureIds[point];

          for(int32_t i = 0; i < m_SelectedWeakPtrVector.size(); i++)
          {
            EXECUTE_FUNCTION_TEMPLATE(this, copyCellArraysToFaceArrays, m_SelectedWeakPtrVector[i].lock(), triangleIndex, point, point, m_SelectedWeakPtrVector[i].lock(),
                                      m_CreatedWeakPtrVector[i].lock(), true)
          }

          triangleIndex++;

          ownerLists[m_NodeIds[nodeId1]].insert(m_FeatureIds[point]);
          ownerLists[m_NodeIds[nodeId1]].insert(-1);
          ownerLists[m_NodeIds[nodeId2]].insert(m_FeatureIds[point]);
          ownerLists[m_NodeIds[nodeId2]].insert(-1);
          ownerLists[m_NodeIds[nodeId3]].insert(m_FeatureIds[point]);
          ownerLists[m_NodeIds[nodeId3]].insert(-1);
          ownerLists[m_NodeIds[nodeId4]].insert(m_FeatureIds[point]);
          ownerLists[m_NodeIds[nodeId4]].insert(-1);
        }
        else if(m_FeatureIds[point] != m_FeatureIds[neigh2])
        {
          nodeId1 = (k * (xP + 1) * (yP + 1)) + ((j + 1) * (xP + 1)) + (i + 1);
          getGridCoordinates(grid, i + 1, j + 1, k, vertex + (m_NodeIds[nodeId1] * 3));

          nodeId2 = (k * (xP + 1) * (yP + 1)) + ((j + 1) * (xP + 1)) + i;
          getGridCoordinates(grid, i, j + 1, k, vertex + (m_NodeIds[nodeId2] * 3));

          nodeId3 = ((k + 1) * (xP + 1) * (yP + 1)) + ((j + 1) * (xP + 1)) + (i + 1);
          getGridCoordinates(grid, i + 1, j + 1, k + 1, vertex + (m_NodeIds[nodeId3] * 3));

          nodeId4 = ((k + 1) * (xP + 1) * (yP + 1)) + ((j + 1) * (xP + 1)) + i;
          getGridCoordinates(grid, i, j + 1, k + 1, vertex + (m_NodeIds[nodeId4] * 3));

          triangle[triangleIndex * 3 + 0] = m_NodeIds[nodeId1];
          triangle[triangleIndex * 3 + 1] = m_NodeIds[nodeId3];
          triangle[triangleIndex * 3 + 2] = m_NodeIds[nodeId2];
          m_FaceLabels[triangleIndex * 2] = m_FeatureIds[neigh2];
          m_FaceLabels[triangleIndex * 2 + 1] = m_FeatureIds[point];
          cIndex1 = neigh2;
          cIndex2 = point;
          if(m_FeatureIds[point] < m_FeatureIds[neigh2])
          {
            triangle[triangleIndex * 3 + 1] = m_NodeIds[nodeId2];
            triangle[triangleIndex * 3 + 2] = m_NodeIds[nodeId3];
            m_FaceLabels[triangleIndex * 2] = m_FeatureIds[point];
            m_FaceLabels[triangleIndex * 2 + 1] = m_FeatureIds[neigh2];
            cIndex1 = point;
            cIndex2 = neigh2;
          }

          for(int32_t i = 0; i < m_SelectedWeakPtrVector.size(); i++)
          {
            EXECUTE_FUNCTION_TEMPLATE(this, copyCellArraysToFaceArrays, m_SelectedWeakPtrVector[i].lock(), triangleIndex, neigh2, point, m_SelectedWeakPtrVector[i].lock(),
                                      m_CreatedWeakPtrVector[i].lock())
          }

          triangleIndex++;

          triangle[triangleIndex * 3 + 0] = m_NodeIds[nodeId2];
          triangle[triangleIndex * 3 + 1] = m_NodeIds[nodeId3];
          triangle[triangleIndex * 3 + 2] = m_NodeIds[nodeId4];
          m_FaceLabels[triangleIndex * 2] = m_FeatureIds[neigh2];
          m_FaceLabels[triangleIndex * 2 + 1] = m_FeatureIds[point];
          cIndex1 = neigh2;
          cIndex2 = point;
          if(m_FeatureIds[point] < m_FeatureIds[neigh2])
          {
            triangle[triangleIndex * 3 + 1] = m_NodeIds[nodeId4];
            triangle[triangleIndex * 3 + 2] = m_NodeIds[nodeId3];
            m_FaceLabels[triangleIndex * 2] = m_FeatureIds[point];
            m_FaceLabels[triangleIndex * 2 + 1] = m_FeatureIds[neigh2];
            cIndex1 = point;
            cIndex2 = neigh2;
          }

          for(int32_t i = 0; i < m_SelectedWeakPtrVector.size(); i++)
          {
            EXECUTE_FUNCTION_TEMPLATE(this, copyCellArraysToFaceArrays, m_SelectedWeakPtrVector[i].lock(), triangleIndex, neigh2, point, m_SelectedWeakPtrVector[i].lock(),
                                      m_CreatedWeakPtrVector[i].lock())
          }

          triangleIndex++;

          ownerLists[m_NodeIds[nodeId1]].insert(m_FeatureIds[point]);
          ownerLists[m_NodeIds[nodeId1]].insert(m_FeatureIds[neigh2]);
          ownerLists[m_NodeIds[nodeId2]].insert(m_FeatureIds[point]);
          ownerLists[m_NodeIds[nodeId2]].insert(m_FeatureIds[neigh2]);
          ownerLists[m_NodeIds[nodeId3]].insert(m_FeatureIds[point]);
          ownerLists[m_NodeIds[nodeId3]].insert(m_FeatureIds[neigh2]);
          ownerLists[m_NodeIds[nodeId4]].insert(m_FeatureIds[point]);
          ownerLists[m_NodeIds[nodeId4]].insert(m_FeatureIds[neigh2]);
        }
        if(k == (zP - 1)) // Takes care of the end of a Pillar
        {
          nodeId1 = ((k + 1) * (xP + 1) * (yP + 1)) + (j * (xP + 1)) + (i + 1);
          getGridCoordinates(grid, i + 1, j, k + 1, vertex + (m_NodeIds[nodeId1] * 3));

          nodeId2 = ((k + 1) * (xP + 1) * (yP + 1)) + (j * (xP + 1)) + i;
          getGridCoordinates(grid, i, j, k + 1, vertex + (m_NodeIds[nodeId2] * 3));

          nodeId3 = ((k + 1) * (xP + 1) * (yP + 1)) + ((j + 1) * (xP + 1)) + (i + 1);
          getGridCoordinates(grid, i + 1, j + 1, k + 1, vertex + (m_NodeIds[nodeId3] * 3));

          nodeId4 = ((k + 1) * (xP + 1) * (yP + 1)) + ((j + 1) * (xP + 1)) + i;
          getGridCoordinates(grid, i, j + 1, k + 1, vertex + (m_NodeIds[nodeId4] * 3));

          triangle[triangleIndex * 3 + 0] = m_NodeIds[nodeId1];
          triangle[triangleIndex * 3 + 1] = m_NodeIds[nodeId3];
          triangle[triangleIndex * 3 + 2] = m_NodeIds[nodeId2];
          m_FaceLabels[triangleIndex * 2] = -1;
          m_FaceLabels[triangleIndex * 2 + 1] = m_FeatureIds[point];

          for(int32_t i = 0; i < m_SelectedWeakPtrVector.size(); i++)
          {
            EXECUTE_FUNCTION_TEMPLATE(this, copyCellArraysToFaceArrays, m_SelectedWeakPtrVector[i].lock(), triangleIndex, point, point, m_SelectedWeakPtrVector[i].lock(),
                                      m_CreatedWeakPtrVector[i].lock(), true)
          }

          triangleIndex++;

          triangle[triangleIndex * 3 + 0] = m_NodeIds[nodeId2];
          triangle[triangleIndex * 3 + 1] = m_NodeIds[nodeId3];
          triangle[triangleIndex * 3 + 2] = m_NodeIds[nodeId4];
          m_FaceLabels[triangleIndex * 2] = -1;
          m_FaceLabels[triangleIndex * 2 + 1] = m_FeatureIds[point];

          for(int32_t i = 0; i < m_SelectedWeakPtrVector.size(); i++)
          {
            EXECUTE_FUNCTION_TEMPLATE(this, copyCellArraysToFaceArrays, m_SelectedWeakPtrVector[i].lock(), triangleIndex, point, point, m_SelectedWeakPtrVector[i].lock(),
                                      m_CreatedWeakPtrVector[i].lock(), true)
          }

          triangleIndex++;

          ownerLists[m_NodeIds[nodeId1]].insert(m_FeatureIds[point]);
          ownerLists[m_NodeIds[nodeId1]].insert(-1);
          ownerLists[m_NodeIds[nodeId2]].insert(m_FeatureIds[point]);
          ownerLists[m_NodeIds[nodeId2]].insert(-1);
          ownerLists[m_NodeIds[nodeId3]].insert(m_FeatureIds[point]);
          ownerLists[m_NodeIds[nodeId3]].insert(-1);
          ownerLists[m_NodeIds[nodeId4]].insert(m_FeatureIds[point]);
          ownerLists[m_NodeIds[nodeId4]].insert(-1);
        }
        else if(m_FeatureIds[point] != m_FeatureIds[neigh3])
        {
          nodeId1 = ((k + 1) * (xP + 1) * (yP + 1)) + (j * (xP + 1)) + (i + 1);
          getGridCoordinates(grid, i + 1, j, k + 1, vertex + (m_NodeIds[nodeId1] * 3));

          nodeId2 = ((k + 1) * (xP + 1) * (yP + 1)) + (j * (xP + 1)) + i;
          getGridCoordinates(grid, i, j, k + 1, vertex + (m_NodeIds[nodeId2] * 3));

          nodeId3 = ((k + 1) * (xP + 1) * (yP + 1)) + ((j + 1) * (xP + 1)) + (i + 1);
          getGridCoordinates(grid, i + 1, j + 1, k + 1, vertex + (m_NodeIds[nodeId3] * 3));

          nodeId4 = ((k + 1) * (xP + 1) * (yP + 1)) + ((j + 1) * (xP + 1)) + i;
          getGridCoordinates(grid, i, j + 1, k + 1, vertex + (m_NodeIds[nodeId4] * 3));

          triangle[triangleIndex * 3 + 0] = m_NodeIds[nodeId1];
          triangle[triangleIndex * 3 + 1] = m_NodeIds[nodeId2];
          triangle[triangleIndex * 3 + 2] = m_NodeIds[nodeId3];
          m_FaceLabels[triangleIndex * 2] = m_FeatureIds[neigh3];
          m_FaceLabels[triangleIndex * 2 + 1] = m_FeatureIds[point];
          cIndex1 = neigh3;
          cIndex2 = point;
          if(m_FeatureIds[point] < m_FeatureIds[neigh3])
          {
            triangle[triangleIndex * 3 + 1] = m_NodeIds[nodeId3];
            triangle[triangleIndex * 3 + 2] = m_NodeIds[nodeId2];
            m_FaceLabels[triangleIndex * 2] = m_FeatureIds[point];
            m_FaceLabels[triangleIndex * 2 + 1] = m_FeatureIds[neigh3];
            cIndex1 = point;
            cIndex2 = neigh3;
          }

          for(int32_t i = 0; i < m_SelectedWeakPtrVector.size(); i++)
          {
            EXECUTE_FUNCTION_TEMPLATE(this, copyCellArraysToFaceArrays, m_SelectedWeakPtrVector[i].lock(), triangleIndex, neigh3, point, m_SelectedWeakPtrVector[i].lock(),
                                      m_CreatedWeakPtrVector[i].lock())
          }

          triangleIndex++;

          triangle[triangleIndex * 3 + 0] = m_NodeIds[nodeId2];
          triangle[triangleIndex * 3 + 1] = m_NodeIds[nodeId4];
          triangle[triangleIndex * 3 + 2] = m_NodeIds[nodeId3];
          m_FaceLabels[triangleIndex * 2] = m_FeatureIds[neigh3];
          m_FaceLabels[triangleIndex * 2 + 1] = m_FeatureIds[point];
          cIndex1 = neigh3;
          cIndex2 = point;
          if(m_FeatureIds[point] < m_FeatureIds[neigh3])
          {
            triangle[triangleIndex * 3 + 1] = m_NodeIds[nodeId3];
            triangle[triangleIndex * 3 + 2] = m_NodeIds[nodeId4];
            m_FaceLabels[triangleIndex * 2] = m_FeatureIds[point];
            m_FaceLabels[triangleIndex * 2 + 1] = m_FeatureIds[neigh3];
            cIndex1 = point;
            cIndex2 = neigh3;
          }

          for(int32_t i = 0; i < m_SelectedWeakPtrVector.size(); i++)
          {
            EXECUTE_FUNCTION_TEMPLATE(this, copyCellArraysToFaceArrays, m_SelectedWeakPtrVector[i].lock(), triangleIndex, neigh3, point, m_SelectedWeakPtrVector[i].lock(),
                                      m_CreatedWeakPtrVector[i].lock())
          }

          triangleIndex++;

          ownerLists[m_NodeIds[nodeId1]].insert(m_FeatureIds[point]);
          ownerLists[m_NodeIds[nodeId1]].insert(m_FeatureIds[neigh3]);
          ownerLists[m_NodeIds[nodeId2]].insert(m_FeatureIds[point]);
          ownerLists[m_NodeIds[nodeId2]].insert(m_FeatureIds[neigh3]);
          ownerLists[m_NodeIds[nodeId3]].insert(m_FeatureIds[point]);
          ownerLists[m_NodeIds[nodeId3]].insert(m_FeatureIds[neigh3]);
          ownerLists[m_NodeIds[nodeId4]].insert(m_FeatureIds[point]);
          ownerLists[m_NodeIds[nodeId4]].insert(m_FeatureIds[neigh3]);
        }
      }
    }
  }

  for(int64_t i = 0; i < nodeCount; i++)
  {
    m_NodeTypes[i] = ownerLists[i].size();
    if(m_NodeTypes[i] > 4)
    {
      m_NodeTypes[i] = 4;
    }
    if(ownerLists[i].find(-1) != ownerLists[i].end())
    {
      m_NodeTypes[i] += 10;
    }
  }

}

// -----------------------------------------------------------------------------
//
// -----------------------------------------------------------------------------
void QuickSurfaceMesh::execute()
{
  clearErrorCode();
  clearWarningCode();
  dataCheck();
  if(getErrorCode() < 0)
  {
    return;
  }

  DataContainer::Pointer m = getDataContainerArray()->getDataContainer(m_FeatureIdsArrayPath.getDataContainerName());
  if(getErrorCode() < 0)
  {
    return;
  }
  DataContainer::Pointer sm = getDataContainerArray()->getDataContainer(getSurfaceDataContainerName());
  if(getErrorCode() < 0)
  {
    return;
  }
  DataContainer::Pointer tripleLineDC = getDataContainerArray()->getDataContainer(getTripleLineDataContainerName());
  if(getErrorCode() < 0)
  {
    return;
  }
  IGeometryGrid::Pointer grid = m->getGeometryAs<IGeometryGrid>();

  size_t udims[3] = {0, 0, 0};
  std::tie(udims[0], udims[1], udims[2]) = grid->getDimensions();

  int64_t dims[3] = {
      static_cast<int64_t>(udims[0]),
      static_cast<int64_t>(udims[1]),
      static_cast<int64_t>(udims[2]),
  };

  int64_t xP = dims[0];
  int64_t yP = dims[1];
  int64_t zP = dims[2];

  std::vector<std::set<int32_t>> ownerLists;

  int64_t possibleNumNodes = (xP + 1) * (yP + 1) * (zP + 1);
  std::vector<int64_t> m_NodeIds(possibleNumNodes, -1);

  int64_t nodeCount = 0;
  int64_t triangleCount = 0;

  correctProblemVoxels();

  determineActiveNodes(m_NodeIds, nodeCount, triangleCount);

  // now create node and triangle arrays knowing the number that will be needed
  TriangleGeom::Pointer triangleGeom = sm->getGeometryAs<TriangleGeom>();
  triangleGeom->resizeTriList(triangleCount);
  triangleGeom->resizeVertexList(nodeCount);

  createNodesAndTriangles(m_NodeIds, nodeCount, triangleCount);

  int64_t* triangle = triangleGeom->getTriPointer(0);

  FloatArrayType::Pointer vertices = triangleGeom->getVertices();
  SharedEdgeList::Pointer edges = EdgeGeom::CreateSharedEdgeList(0);
  EdgeGeom::Pointer edgeGeom = EdgeGeom::CreateGeometry(edges, vertices, SIMPL::Geometry::EdgeGeometry);
  tripleLineDC->setGeometry(edgeGeom);

  int64_t edgeCount = 0;
  for(int64_t i = 0; i < triangleCount; i++)
  {
    int64_t n1 = triangle[3 * i + 0];
    int64_t n2 = triangle[3 * i + 1];
    int64_t n3 = triangle[3 * i + 2];
    if(m_NodeTypes[n1] >= 3 && m_NodeTypes[n2] >= 3)
    {
      edgeCount++;
    }
    if(m_NodeTypes[n1] >= 3 && m_NodeTypes[n3] >= 3)
    {
      edgeCount++;
    }
    if(m_NodeTypes[n2] >= 3 && m_NodeTypes[n3] >= 3)
    {
      edgeCount++;
    }
  }

  edgeGeom->resizeEdgeList(edgeCount);
  int64_t* edge = edgeGeom->getEdgePointer(0);
  edgeCount = 0;
  for(int64_t i = 0; i < triangleCount; i++)
  {
    int64_t n1 = triangle[3 * i + 0];
    int64_t n2 = triangle[3 * i + 1];
    int64_t n3 = triangle[3 * i + 2];
    if(m_NodeTypes[n1] >= 3 && m_NodeTypes[n2] >= 3)
    {
      edge[2 * edgeCount] = n1;
      edge[2 * edgeCount + 1] = n2;
      edgeCount++;
    }
    if(m_NodeTypes[n1] >= 3 && m_NodeTypes[n3] >= 3)
    {
      edge[2 * edgeCount] = n1;
      edge[2 * edgeCount + 1] = n3;
      edgeCount++;
    }
    if(m_NodeTypes[n2] >= 3 && m_NodeTypes[n3] >= 3)
    {
      edge[2 * edgeCount] = n2;
      edge[2 * edgeCount + 1] = n3;
      edgeCount++;
    }
  }
}

// -----------------------------------------------------------------------------
//
// -----------------------------------------------------------------------------
void QuickSurfaceMesh::generateTripleLines()
{

  /**
   * This is a bit of experimental code where we define a triple line as an edge
   * that shares voxels with at least 3 unique Feature Ids. This is different
   * than saying that an edge is part of a triple line if it's nodes are considered
   * sharing at least 3 unique voxels. This code is not complete as it will only
   * find "interior" triple lines and no lines on the surface. I am going to leave
   * this bit of code in place for historical reasons so that we can refer to it
   * later if needed.
   * Mike Jackson, JULY 2018
   */
  Q_ASSERT(false); // We don't want anyone to run this program.
  DataContainer::Pointer m = getDataContainerArray()->getDataContainer(m_FeatureIdsArrayPath.getDataContainerName());
  DataContainer::Pointer sm = getDataContainerArray()->getDataContainer(getSurfaceDataContainerName());

  AttributeMatrix::Pointer featAttrMat = sm->getAttributeMatrix(m_FeatureAttributeMatrixName);
  size_t numFeatures = 0;
  size_t numTuples = m_FeatureIdsPtr.lock()->getNumberOfTuples();
  for(size_t i = 0; i < numTuples; i++)
  {
    if(m_FeatureIds[i] > numFeatures)
    {
      numFeatures = m_FeatureIds[i];
    }
  }

  QVector<size_t> featDims(1, numFeatures + 1);
  featAttrMat->setTupleDimensions(featDims);

  IGeometryGrid::Pointer grid = m->getGeometryAs<IGeometryGrid>();
  ImageGeom::Pointer imageGeom = m->getGeometryAs<ImageGeom>();

  size_t udims[3] = {0, 0, 0};
  std::tie(udims[0], udims[1], udims[2]) = grid->getDimensions();

  int64_t dims[3] = {
      static_cast<int64_t>(udims[0]),
      static_cast<int64_t>(udims[1]),
      static_cast<int64_t>(udims[2]),
  };

  int64_t xP = dims[0];
  int64_t yP = dims[1];
  int64_t zP = dims[2];
  int64_t point = 0, neigh1 = 0, neigh2 = 0, neigh3 = 0;

  std::set<int32_t> uFeatures;

  FloatVec3Type origin = {0.0f, 0.0f, 0.0f};
  std::tie(origin[0], origin[1], origin[2]) = imageGeom->getOrigin();
  FloatVec3Type res = {0.0f, 0.0f, 0.0f};
  std::tie(res[0], res[1], res[2]) = imageGeom->getSpacing();

  VertexMap vertexMap;
  EdgeMap edgeMap;
  int64_t vertCounter = 0;
  int64_t edgeCounter = 0;

  // Cycle through again assigning coordinates to each node and assigning node numbers and feature labels to each triangle
  // int64_t triangleIndex = 0;
  for(int64_t k = 0; k < zP - 1; k++)
  {
    for(int64_t j = 0; j < yP - 1; j++)
    {
      for(int64_t i = 0; i < xP - 1; i++)
      {

        point = (k * xP * yP) + (j * xP) + i;
        // Case 1
        neigh1 = point + 1;
        neigh2 = point + (xP * yP) + 1;
        neigh3 = point + (xP * yP);

        Vertex p0 = {{origin[0] + static_cast<float>(i) * res[0] + res[0], origin[1] + static_cast<float>(j) * res[1] + res[1], origin[2] + static_cast<float>(k) * res[2] + res[2]}};

        Vertex p1 = {{origin[0] + static_cast<float>(i) * res[0] + res[0], origin[1] + static_cast<float>(j) * res[1], origin[2] + static_cast<float>(k) * res[2] + res[2]}};

        Vertex p2 = {{origin[0] + static_cast<float>(i) * res[0], origin[1] + static_cast<float>(j) * res[1] + res[1], origin[2] + static_cast<float>(k) * res[2] + res[2]}};

        Vertex p3 = {{origin[0] + static_cast<float>(i) * res[0] + res[0], origin[1] + static_cast<float>(j) * res[1] + res[1], origin[2] + static_cast<float>(k) * res[2]}};

        uFeatures.clear();
        uFeatures.insert(m_FeatureIds[point]);
        uFeatures.insert(m_FeatureIds[neigh1]);
        uFeatures.insert(m_FeatureIds[neigh2]);
        uFeatures.insert(m_FeatureIds[neigh3]);

        if(uFeatures.size() > 2)
        {
          auto iter = vertexMap.find(p0);
          if(iter == vertexMap.end())
          {
            vertexMap[p0] = vertCounter++;
          }
          iter = vertexMap.find(p1);
          if(iter == vertexMap.end())
          {
            vertexMap[p1] = vertCounter++;
          }
          int64_t i0 = vertexMap[p0];
          int64_t i1 = vertexMap[p1];

          Edge tmpEdge = {{i0, i1}};
          auto eiter = edgeMap.find(tmpEdge);
          if(eiter == edgeMap.end())
          {
            edgeMap[tmpEdge] = edgeCounter++;
          }
        }

        // Case 2
        neigh1 = point + xP;
        neigh2 = point + (xP * yP) + xP;
        neigh3 = point + (xP * yP);

        uFeatures.clear();
        uFeatures.insert(m_FeatureIds[point]);
        uFeatures.insert(m_FeatureIds[neigh1]);
        uFeatures.insert(m_FeatureIds[neigh2]);
        uFeatures.insert(m_FeatureIds[neigh3]);
        if(uFeatures.size() > 2)
        {
          auto iter = vertexMap.find(p0);
          if(iter == vertexMap.end())
          {
            vertexMap[p0] = vertCounter++;
          }
          iter = vertexMap.find(p2);
          if(iter == vertexMap.end())
          {
            vertexMap[p2] = vertCounter++;
          }

          int64_t i0 = vertexMap[p0];
          int64_t i2 = vertexMap[p2];

          Edge tmpEdge = {{i0, i2}};
          auto eiter = edgeMap.find(tmpEdge);
          if(eiter == edgeMap.end())
          {
            edgeMap[tmpEdge] = edgeCounter++;
          }
        }

        // Case 3
        neigh1 = point + 1;
        neigh2 = point + xP + 1;
        neigh3 = point + +xP;

        uFeatures.clear();
        uFeatures.insert(m_FeatureIds[point]);
        uFeatures.insert(m_FeatureIds[neigh1]);
        uFeatures.insert(m_FeatureIds[neigh2]);
        uFeatures.insert(m_FeatureIds[neigh3]);
        if(uFeatures.size() > 2)
        {
          auto iter = vertexMap.find(p0);
          if(iter == vertexMap.end())
          {
            vertexMap[p0] = vertCounter++;
          }
          iter = vertexMap.find(p3);
          if(iter == vertexMap.end())
          {
            vertexMap[p3] = vertCounter++;
          }

          int64_t i0 = vertexMap[p0];
          int64_t i3 = vertexMap[p3];

          Edge tmpEdge = {{i0, i3}};
          auto eiter = edgeMap.find(tmpEdge);
          if(eiter == edgeMap.end())
          {
            edgeMap[tmpEdge] = edgeCounter++;
          }
        }
      }
    }
  }

  EdgeGeom::Pointer tripleLineEdge = EdgeGeom::New();
  SharedVertexList::Pointer vertices = tripleLineEdge->CreateSharedVertexList(vertexMap.size() * 3);

  for(auto vert : vertexMap)
  {
    float v0 = vert.first[0];
    float v1 = vert.first[1];
    float v2 = vert.first[2];
    int64_t idx = vert.second;
    vertices->setComponent(idx, 0, v0);
    vertices->setComponent(idx, 1, v1);
    vertices->setComponent(idx, 2, v2);
  }

  tripleLineEdge->setVertices(vertices);

  SharedEdgeList::Pointer edges = tripleLineEdge->CreateSharedEdgeList(edgeMap.size() * 2);
  for(auto edge : edgeMap)
  {
    int64_t i0 = edge.first[0];
    int64_t i1 = edge.first[1];
    int64_t idx = edge.second;
    edges->setComponent(idx, 0, i0);
    edges->setComponent(idx, 1, i1);
  }
  tripleLineEdge->setEdges(edges);

  DataContainerArray::Pointer dca = getDataContainerArray();
  DataContainer::Pointer dc = DataContainer::New("Edges");
  dca->addOrReplaceDataContainer(dc);
  dc->setGeometry(tripleLineEdge);
}

// -----------------------------------------------------------------------------
//
// -----------------------------------------------------------------------------
AbstractFilter::Pointer QuickSurfaceMesh::newFilterInstance(bool copyFilterParameters) const
{
  QuickSurfaceMesh::Pointer filter = QuickSurfaceMesh::New();
  if(copyFilterParameters)
  {
    copyFilterParameterInstanceVariables(filter.get());
  }
  return filter;
}

// -----------------------------------------------------------------------------
//
// -----------------------------------------------------------------------------
const QString QuickSurfaceMesh::getCompiledLibraryName() const
{
  return SurfaceMeshingConstants::SurfaceMeshingBaseName;
}

// -----------------------------------------------------------------------------
//
// -----------------------------------------------------------------------------
const QString QuickSurfaceMesh::getBrandingString() const
{
  return "SurfaceMeshing";
}

// -----------------------------------------------------------------------------
//
// -----------------------------------------------------------------------------
const QString QuickSurfaceMesh::getFilterVersion() const
{
  QString version;
  QTextStream vStream(&version);
  vStream << SurfaceMeshing::Version::Major() << "." << SurfaceMeshing::Version::Minor() << "." << SurfaceMeshing::Version::Patch();
  return version;
}
// -----------------------------------------------------------------------------
//
// -----------------------------------------------------------------------------
const QString QuickSurfaceMesh::getGroupName() const
{
  return SIMPL::FilterGroups::SurfaceMeshingFilters;
}

// -----------------------------------------------------------------------------
//
// -----------------------------------------------------------------------------
const QUuid QuickSurfaceMesh::getUuid()
{
  return QUuid("{07b49e30-3900-5c34-862a-f1fb48bad568}");
}

// -----------------------------------------------------------------------------
//
// -----------------------------------------------------------------------------
const QString QuickSurfaceMesh::getSubGroupName() const
{
  return SIMPL::FilterSubGroups::GenerationFilters;
}

// -----------------------------------------------------------------------------
//
// -----------------------------------------------------------------------------
const QString QuickSurfaceMesh::getHumanLabel() const
{
  return "Quick Surface Mesh";
}
