//                                               -*- C++ -*-
/**
 *  @brief Mesh is defined as a collection of n-D vertices and simplices
 *
 *  Copyright 2005-2018 Airbus-EDF-IMACS-Phimeca
 *
 *  This library is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU Lesser General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  along with this library.  If not, see <http://www.gnu.org/licenses/>.
 *
 */
#include <fstream>
#include <algorithm>

#include "openturns/Mesh.hxx"
#include "openturns/PersistentObjectFactory.hxx"
#include "openturns/Log.hxx"
#include "openturns/Os.hxx"
#include "openturns/Exception.hxx"
#include "openturns/Graph.hxx"
#include "openturns/Cloud.hxx"
#include "openturns/Curve.hxx"
#include "openturns/Polygon.hxx"
#include "openturns/PolygonArray.hxx"
#include "openturns/Collection.hxx"
#include "openturns/SpecFunc.hxx"
#include "openturns/PlatformInfo.hxx"

BEGIN_NAMESPACE_OPENTURNS

CLASSNAMEINIT(Mesh)

static const Factory<Mesh> Factory_Mesh;

/* Default constructor */
Mesh::Mesh(const UnsignedInteger dimension)
  : DomainImplementation(dimension)
  , vertices_(1, dimension) // At least one point
  , simplices_()
  , tree_()
  , verticesToSimplices_(0)
  , lowerBoundingBoxSimplices_(0, dimension)
  , upperBoundingBoxSimplices_(0, dimension)
  , isAlreadyComputedVolume_(false)
  , volume_(0.0)
{
  // Nothing to do
  if (vertices_.getDescription().isBlank()) vertices_.setDescription(Description::BuildDefault(dimension, "t"));
}

/* Parameters constructor, simplified interface for 1D case */
Mesh::Mesh(const Sample & vertices)
  : DomainImplementation(vertices.getDimension())
  , vertices_(0, vertices.getDimension())
  , simplices_(0)
  , tree_()
  , verticesToSimplices_(0)
  , lowerBoundingBoxSimplices_(0, vertices.getDimension())
  , upperBoundingBoxSimplices_(0, vertices.getDimension())
  , isAlreadyComputedVolume_(false)
  , volume_(0.0)
{
  // Use the vertices accessor to initialize the kd-tree
  setVertices(vertices);
}

/* Parameters constructor, simplified interface for 1D case */
Mesh::Mesh(const Sample & vertices,
           const IndicesCollection & simplices)
  : DomainImplementation(vertices.getDimension())
  , vertices_(0, vertices.getDimension())
  , simplices_(simplices)
  , tree_()
  , verticesToSimplices_(0)
  , lowerBoundingBoxSimplices_(0, vertices.getDimension())
  , upperBoundingBoxSimplices_(0, vertices.getDimension())
  , isAlreadyComputedVolume_(false)
  , volume_(0.0)
{
  // Use the vertices accessor to initialize the kd-tree
  setVertices(vertices);
}

/* Clone method */
Mesh * Mesh::clone() const
{
  return new Mesh(*this);
}

/* Description of the vertices accessor */
Description Mesh::getDescription() const
{
  return vertices_.getDescription();
}

/* Vertices accessor */
Sample Mesh::getVertices() const
{
  return vertices_;
}

void Mesh::setVertices(const Sample & vertices)
{
  isAlreadyComputedVolume_ = false;
  vertices_ = vertices;
  if (vertices_.getDescription().isBlank()) vertices_.setDescription(Description::BuildDefault(vertices_.getDimension(), "t"));
  verticesToSimplices_ = IndicesCollection(0);
  lowerBoundingBoxSimplices_ = Sample(0, dimension_);
  upperBoundingBoxSimplices_ = Sample(0, dimension_);
}

/* Compute KDTree to speed-up searches */
void Mesh::computeKDTree()
{
  tree_ = KDTree(vertices_);
}

/* Vertex accessor */
Point Mesh::getVertex(const UnsignedInteger index) const
{
  if (index >= getVerticesNumber()) throw InvalidArgumentException(HERE) << "Error: the vertex index=" << index << " must be less than the number of vertices=" << getVerticesNumber();
  return vertices_[index];
}

void Mesh::setVertex(const UnsignedInteger index,
                     const Point & vertex)
{
  isAlreadyComputedVolume_ = false;
  vertices_[index] = vertex;
}

/* Simplices accessor */
Mesh::IndicesCollection Mesh::getSimplices() const
{
  return simplices_;
}

void Mesh::setSimplices(const IndicesCollection & simplices)
{
  if (!(simplices == simplices_))
  {
    simplices_ = simplices;
    verticesToSimplices_ = IndicesCollection(0);
    lowerBoundingBoxSimplices_ = Sample(0, dimension_);
    upperBoundingBoxSimplices_ = Sample(0, dimension_);
  }
}

/* Simplex accessor */
Indices Mesh::getSimplex(const UnsignedInteger index) const
{
  if (index >= getSimplicesNumber()) throw InvalidArgumentException(HERE) << "Error: the simplex index=" << index << " must be less than the number of simplices=" << getSimplicesNumber();
  return simplices_[index];
}

/* Check the mesh validity */
void Mesh::checkValidity() const
{
  // Check the vertices: no duplicate, no unused vertex
  // Check the simplices: no simplex with duplicate vertices, no simplex with unknown vertex, no simplex with a number of vertices different from dimension+1
  for (UnsignedInteger i = 0; i < getSimplicesNumber(); ++ i)
  {
    Indices simplex(simplices_[i]);
    if (simplex.getSize() != getDimension() + 1)
      throw InvalidArgumentException(HERE) << "Error: mesh has dimension " << getDimension() << " but simplex #" << i << " has size" << simplex.getSize();

    if (!simplex.check(getVerticesNumber()))
      throw InvalidArgumentException(HERE) << "Error: mesh has " << getVerticesNumber() << " vertices but simplex #" << i << " refers to an unknown vertex";
  }
  // Check that no ball can be included into the intersection of two simplices
}

Bool Mesh::isValid() const
{
  Bool result = true;
  try
  {
    checkValidity();
  }
  catch (Exception)
  {
    result = false;
  }
  return result;
}

/* Check if the given point is in the mesh */
Bool Mesh::contains(const Point & point) const
{
  // First, check against the bounding box
  if (!Interval(getLowerBound(), getUpperBound()).contains(point)) return false;
  // Second, check the simplices containing the nearest vertex
  const UnsignedInteger nearestIndex = getNearestVertexIndex(point);
  // To be sure that the vertices to simplices map is up to date
  if (verticesToSimplices_.getSize() == 0) (void) getVerticesToSimplicesMap();
  const Indices simplicesCandidates(verticesToSimplices_[nearestIndex]);
  for (UnsignedInteger i = 0; i < simplicesCandidates.getSize(); ++i)
    if (checkPointInSimplex(point, simplicesCandidates[i])) return true;
  // Third, a full loop to deal with points not inside of a simplex associated to the nearest vertex
  const UnsignedInteger simplicesSize = getSimplicesNumber();
  for (UnsignedInteger i = 0; i < simplicesSize; ++i) if (checkPointInSimplex(point, i)) return true;
  return false;
}

/* Check if the given point is in the given simplex */
SquareMatrix Mesh::buildSimplexMatrix(const UnsignedInteger index) const
{
  if (index >= getSimplicesNumber()) throw InvalidArgumentException(HERE) << "Error: the simplex index=" << index << " must be less than the number of simplices=" << getSimplicesNumber();
  SquareMatrix matrix(dimension_ + 1);
  const Indices vertexIndices(simplices_[index]);
  // Loop over the vertices of the simplex
  for (UnsignedInteger j = 0; j <= dimension_; ++j)
  {
    const Point vertexJ(vertices_[vertexIndices[j]]);
    for (UnsignedInteger i = 0; i < dimension_; ++i) matrix(i, j) = vertexJ[i];
    matrix(dimension_, j) = 1.0;
  }
  return matrix;
}

/* Check if the given point is in the given simplex */
Bool Mesh::checkPointInSimplex(const Point & point,
                               const UnsignedInteger index) const
{
  Point alpha;
  return checkPointInSimplexWithCoordinates(point, index, alpha);
}

/* Check if the given point is in the given simplex and returns its barycentric coordinates */
Bool Mesh::checkPointInSimplexWithCoordinates(const Point & point,
    const UnsignedInteger index,
    Point & coordinates) const
{
  // Exit early if bounding boxes have been computed and point is outside bounding box
  if (lowerBoundingBoxSimplices_.getSize() > 0)
  {
    for (UnsignedInteger i = 0; i < dimension_; ++i)
    {
      if(point[i] < lowerBoundingBoxSimplices_(index, i))
        return false;
      if(point[i] > upperBoundingBoxSimplices_(index, i))
        return false;
    }
  }
  SquareMatrix matrix(buildSimplexMatrix(index));
  Point v(point);
  v.add(1.0);
  coordinates = matrix.solveLinearSystem(v, false);
  for (UnsignedInteger i = 0; i <= dimension_; ++i) if ((coordinates[i] < 0.0) || (coordinates[i] > 1.0)) return false;
  return true;
}

/* Get the number of vertices */
UnsignedInteger Mesh::getVerticesNumber() const
{
  return vertices_.getSize();
}

/* Get the number of simplices */
UnsignedInteger Mesh::getSimplicesNumber() const
{
  return simplices_.getSize();
}

/* TBB functor to speed-up nearest index computation */
struct NearestFunctor
{
  const Mesh & mesh_;
  const Point & point_;
  Scalar minDistance_;
  UnsignedInteger minIndex_;

  NearestFunctor(const Mesh & mesh, const Point & point)
    : mesh_(mesh), point_(point), minDistance_(SpecFunc::MaxScalar), minIndex_(0) {}

  NearestFunctor(const NearestFunctor & other, TBB::Split)
    : mesh_(other.mesh_), point_(other.point_), minDistance_(SpecFunc::MaxScalar), minIndex_(0) {}

  void operator() (const TBB::BlockedRange<UnsignedInteger> & r)
  {
    for (UnsignedInteger i = r.begin(); i != r.end(); ++i)
    {
      const Scalar d = (point_ - mesh_.getVertices()[i]).normSquare();
      if (d < minDistance_)
      {
        minDistance_ = d;
        minIndex_ = i;
      } // d < minDistance_
    } // i
  } // operator

  void join(const NearestFunctor & other)
  {
    if (other.minDistance_ < minDistance_)
    {
      minDistance_ = other.minDistance_;
      minIndex_ = other.minIndex_;
    } // minDistance
  } // join

}; /* end struct NearestFunctor */

/* Get the index of the nearest vertex */
UnsignedInteger Mesh::getNearestVertexIndex(const Point & point) const
{
  if (point.getDimension() != getDimension()) throw InvalidArgumentException(HERE) << "Error: expected a point of dimension " << getDimension() << ", got a point of dimension " << point.getDimension();
  if (!tree_.isEmpty()) return tree_.getNearestNeighbourIndex(point);
  NearestFunctor functor( *this, point );
  TBB::ParallelReduce( 0, getVerticesNumber(), functor );
  return functor.minIndex_;
}

/* Get the index of the nearest vertex and the index of the containing simplex if any */
Indices Mesh::getNearestVertexAndSimplexIndicesWithCoordinates(const Point & point,
    Point & coordinates) const
{
  if (point.getDimension() != getDimension()) throw InvalidArgumentException(HERE) << "Error: expected a point of dimension " << getDimension() << ", got a point of dimension " << point.getDimension();
  const UnsignedInteger nearestIndex = getNearestVertexIndex(point);
  Indices result(1, nearestIndex);
  // To be sure that the vertices to simplices map is up to date
  if (verticesToSimplices_.getSize() == 0) (void) getVerticesToSimplicesMap();
  const Indices simplicesCandidates(verticesToSimplices_[nearestIndex]);
  coordinates = Point(0);
  for (UnsignedInteger i = 0; i < simplicesCandidates.getSize(); ++i)
  {
    const UnsignedInteger simplexIndex = simplicesCandidates[i];
    if (checkPointInSimplexWithCoordinates(point, simplexIndex, coordinates))
    {
      result.add(simplexIndex);
      break;
    }
  } // Loop over the simplices candidates
  // If no simplex contains the given point, reset the coordinates vector
  if (result.getSize() == 1) coordinates = Point(0);
  return result;
}

/* Get the nearest vertex */
Point Mesh::getNearestVertex(const Point & point) const
{
  return vertices_[getNearestVertexIndex(point)];
}

/* TBB policy to speed-up nearest index computation over a sample */
struct MeshNearestPolicy
{
  const Sample & points_;
  Indices & indices_;
  const Mesh & mesh_;

  MeshNearestPolicy(const Sample & points,
                    Indices & indices,
                    const Mesh & mesh)
    : points_(points)
    , indices_(indices)
    , mesh_(mesh)
  {}

  inline void operator()( const TBB::BlockedRange<UnsignedInteger> & r ) const
  {
    for (UnsignedInteger i = r.begin(); i != r.end(); ++i) indices_[i] = mesh_.getNearestVertexIndex(points_[i]);
  }

}; /* end struct MeshNearestPolicy */

/* Get the index of the nearest vertex for a set of points */
Indices Mesh::getNearestVertexIndex(const Sample & points) const
{
  if (points.getDimension() != getDimension()) throw InvalidArgumentException(HERE) << "Error: expected points of dimension " << getDimension() << ", got points of dimension " << points.getDimension();
  const UnsignedInteger size = points.getSize();
  Indices indices(size);
  if (size == 0) return indices;
  const MeshNearestPolicy policy( points, indices, *this );
  TBB::ParallelFor( 0, size, policy );
  return indices;
}

/* Get the nearest vertex for a set of points */
Sample Mesh::getNearestVertex(const Sample & points) const
{
  const Indices indices(getNearestVertexIndex(points));
  const UnsignedInteger size = indices.getSize();
  Sample neighbours(size, getDimension());
  for (UnsignedInteger i = 0; i < size; ++i)
    neighbours[i] = vertices_[indices[i]];
  return neighbours;
}

/* Compute the volume of a given simplex */
Scalar Mesh::computeSimplexVolume(const UnsignedInteger index) const
{
  if (index >= getSimplicesNumber()) throw InvalidArgumentException(HERE) << "Error: the simplex index=" << index << " must be less than the number of simplices=" << getSimplicesNumber();

  // First special case: 1D simplex
  if (getDimension() == 1)
  {
    const Scalar x0 = vertices_(simplices_[index][0], 0);
    const Scalar x1 = vertices_(simplices_[index][1], 0);
    return std::abs(x1 - x0);
  }
  // Second special case: 2D simplex
  if (getDimension() == 2)
  {
    const Scalar x0 = vertices_(simplices_[index][0], 0);
    const Scalar y0 = vertices_(simplices_[index][0], 1);
    const Scalar x1 = vertices_(simplices_[index][1], 0);
    const Scalar y1 = vertices_(simplices_[index][1], 1);
    const Scalar x2 = vertices_(simplices_[index][2], 0);
    const Scalar y2 = vertices_(simplices_[index][2], 1);
    return 0.5 * std::abs((x2 - x0) * (y1 - y0) - (x0 - x1) * (y2 - y0));
  }
  SquareMatrix matrix(buildSimplexMatrix(index));
  Scalar sign = 0.0;
  return exp(matrix.computeLogAbsoluteDeterminant(sign, false) - SpecFunc::LogGamma(dimension_ + 1));
}

/* Compute P1 gram matrix */
CovarianceMatrix Mesh::computeP1Gram() const
{
  // If no simplex, the P1 gram matrix is null
  if (simplices_.getSize() == 0) return CovarianceMatrix(0);
  const UnsignedInteger simplexSize = getVertices().getDimension() + 1;
  SquareMatrix elementaryGram(simplexSize, Point(simplexSize * simplexSize, 1.0 / SpecFunc::Gamma(simplexSize + 2.0)));
  for (UnsignedInteger i = 0; i < simplexSize; ++i) elementaryGram(i, i) *= 2.0;
  const UnsignedInteger verticesSize = vertices_.getSize();
  const UnsignedInteger simplicesSize = simplices_.getSize();
  SquareMatrix gram(verticesSize);
  for (UnsignedInteger i = 0; i < simplicesSize; ++i)
  {
    const Indices simplex(getSimplex(i));
    const Scalar delta = computeSimplexVolume(i);
    for (UnsignedInteger j = 0; j < simplexSize; ++j)
    {
      const UnsignedInteger newJ = simplex[j];
      for (UnsignedInteger k = 0; k < simplexSize; ++k)
      {
        const UnsignedInteger newK = simplex[k];
        gram(newJ, newK) += delta * elementaryGram(j, k);
      } // Loop over second vertex
    } // Loop over first vertex
  } // Loop over simplices
  return CovarianceMatrix(gram.getImplementation());
}

/* TBB functor to speed-up volume computation */
struct VolumeFunctor
{
  const Mesh & mesh_;
  Scalar accumulator_;

  VolumeFunctor(const Mesh & mesh)
    : mesh_(mesh), accumulator_(0.0) {}

  VolumeFunctor(const VolumeFunctor & other, TBB::Split)
    : mesh_(other.mesh_), accumulator_(0.0) {}

  void operator() (const TBB::BlockedRange<UnsignedInteger> & r)
  {
    for (UnsignedInteger i = r.begin(); i != r.end(); ++i) accumulator_ += mesh_.computeSimplexVolume(i);
  }

  void join(const VolumeFunctor & other)
  {
    accumulator_ += other.accumulator_;
  }

}; /* end struct VolumeFunctor */

/* Compute the volume of the mesh */
Scalar Mesh::computeVolume() const
{
  VolumeFunctor functor( *this );
  TBB::ParallelReduce( 0, getSimplicesNumber(), functor );
  isAlreadyComputedVolume_ = true;
  return functor.accumulator_;
}

/* Get the numerical volume of the domain */
Scalar Mesh::getVolume() const
{
  if (!isAlreadyComputedVolume_) volume_ = computeVolume();
  return volume_;
}

/* Check if the domain is empty, i.e if its numerical volume is zero */
Bool Mesh::isNumericallyEmpty() const
{
  return getVolume() <= ResourceMap::GetAsScalar("Domain-SmallVolume");
}

/* Tells if the mesh is regular */
Bool Mesh::isRegular() const
{
  // For now, only 1D regular meshes are considered
  if (getDimension() != 1) return false;
  const UnsignedInteger size = getSimplicesNumber();
  if (size <= 1) return true;
  Bool regular = true;
  const Scalar epsilon = ResourceMap::GetAsScalar("Mesh-VertexEpsilon");
  const Scalar step = vertices_(simplices_[0][1], 0) - vertices_(simplices_[0][0], 0);
  for (UnsignedInteger i = 1; i < size; ++i)
  {
    regular = regular && (std::abs(vertices_(simplices_[i][1], 0) - vertices_(simplices_[i][0], 0) - step) < epsilon);
    if (!regular) break;
  }
  return regular;
}

/* Lower bound of the bounding box */
Point Mesh::getLowerBound() const
{
  return vertices_.getMin();
}

/* Upper bound of the bounding box */
Point Mesh::getUpperBound() const
{
  return vertices_.getMax();
}


/* Get the map between vertices and simplices: for each vertex, list the vertices indices it belongs to */
Mesh::IndicesCollection Mesh::getVerticesToSimplicesMap() const
{
  if (verticesToSimplices_.getSize() > 0) return verticesToSimplices_;
  const UnsignedInteger numSimplices = getSimplicesNumber();
  const UnsignedInteger numVertices = getVerticesNumber();
  verticesToSimplices_ = IndicesCollection(numVertices, Indices(0));
  lowerBoundingBoxSimplices_ = Sample(numSimplices, Point(dimension_, SpecFunc::MaxScalar));
  upperBoundingBoxSimplices_ = Sample(numSimplices, Point(dimension_, - SpecFunc::MaxScalar));
  for (UnsignedInteger i = 0; i < numSimplices; ++i)
  {
    const Indices simplex(simplices_[i]);
    for (UnsignedInteger j = 0; j < simplex.getSize(); ++j)
    {
      const UnsignedInteger index = simplex[j];
      for(UnsignedInteger k = 0; k < dimension_; ++k)
      {
        if (vertices_(index, k) < lowerBoundingBoxSimplices_(i, k))
          lowerBoundingBoxSimplices_(i, k) = vertices_(index, k);
        if (vertices_(index, k) > upperBoundingBoxSimplices_(i, k))
          upperBoundingBoxSimplices_(i, k) = vertices_(index, k);
      }
      verticesToSimplices_[index].add(i);
    }
  } // Loop over simplices
  return verticesToSimplices_;
}

/* Compute weights such that an integral of a function over the mesh
 * is a weighted sum of its values at the vertices
 */
Point Mesh::computeWeights() const
{
  // First compute the volume of the simplices
  const UnsignedInteger numSimplices = getSimplicesNumber();
  Point simplicesVolume(numSimplices);
  for (UnsignedInteger i = 0; i < numSimplices; ++i)
    simplicesVolume[i] = computeSimplexVolume(i);
  // Second compute the map between vertices and simplices
  const IndicesCollection verticesToSimplices(getVerticesToSimplicesMap());
  // Then compute the weights of the vertices by distributing the volume of each simplex among its vertices
  const UnsignedInteger numVertices = getVerticesNumber();
  Point weights(numVertices, 0.0);
  for (UnsignedInteger i = 0; i < numVertices; ++i)
  {
    const Indices vertexSimplices(verticesToSimplices[i]);
    Scalar weight = 0.0;
    for (UnsignedInteger j = 0; j < vertexSimplices.getSize(); ++j)
      weight += simplicesVolume[vertexSimplices[j]];
    weights[i] = weight;
  } // i
  // Normalize the weights: each simplex has dim+1 vertices, so each vertex
  // get 1/(dim+1) of the volume of the simplices it belongs to
  weights /= (dimension_ + 1.0);
  return weights;
}

/* Comparison operator */
Bool Mesh::operator == (const Mesh & other) const
{
  if (this == &other) return true;
  return (vertices_ == other.vertices_) && (simplices_ == other.simplices_);
}

/* String converter */
String Mesh::__repr__() const
{
  return OSS(true) << "class=" << GetClassName()
         << " name=" << getName()
         << " dimension=" << getDimension()
         << " vertices=" << vertices_.__repr__()
         << " simplices=" << simplices_.__repr__();
}

String Mesh::__str__(const String & offset) const
{
  return __repr__();
}

/* Drawing method */
Graph Mesh::draw() const
{
  if (dimension_ > 3) throw InvalidArgumentException(HERE) << "Error: cannot draw a mesh of dimension > 3.";
  if (dimension_ == 1) return draw1D();
  if (dimension_ == 2) return draw2D();
  if (dimension_ == 3) return draw3D();
  return Graph();
}

Graph Mesh::draw1D() const
{
  checkValidity();
  if (dimension_ != 1) throw InvalidArgumentException(HERE) << "Error: cannot draw a mesh of dimension different from 1 with the draw1D() method.";
  const UnsignedInteger verticesSize = getVerticesNumber();
  const UnsignedInteger simplicesSize = getSimplicesNumber();
  if (verticesSize == 0) throw InvalidArgumentException(HERE) << "Error: cannot draw a mesh with no vertex.";
  Graph graph(String(OSS() << "Mesh " << getName()), "x", "y", true, "topright");
  // The vertices
  Cloud vertices(vertices_, Sample(verticesSize, Point(1, 0.0)));
  vertices.setColor("red");
  vertices.setLegend(String(OSS() << verticesSize << " node" << (verticesSize > 1 ? "s" : "")));
  // The simplices
  for (UnsignedInteger i = 0; i < simplicesSize; ++i)
  {
    Sample data(2, 2);
    data(0, 0) = vertices_(simplices_[i][0], 0);
    data(1, 0) = vertices_(simplices_[i][1], 0);
    Curve simplex(data);
    simplex.setColor("blue");
    if (i == 0) simplex.setLegend(String(OSS() << simplicesSize << " element" << (simplicesSize > 1 ? "s" : "")));
    graph.add(simplex);
  }
  graph.add(vertices);
  return graph;
}

Graph Mesh::draw2D() const
{
  checkValidity();
  const UnsignedInteger verticesSize = getVerticesNumber();
  const UnsignedInteger simplicesSize = getSimplicesNumber();
  if (verticesSize == 0) throw InvalidArgumentException(HERE) << "Error: cannot draw a mesh with no vertex.";
  Graph graph(String(OSS() << "Mesh " << getName()), "x", "y", true, "topright");
  // The vertices
  Cloud vertices(vertices_);
  vertices.setColor("red");
  if (vertices_.getSize() > ResourceMap::GetAsUnsignedInteger("Mesh-LargeSize")) vertices.setPointStyle("dot");
  vertices.setLegend(String(OSS() << verticesSize << " node" << (verticesSize > 1 ? "s" : "")));
  // The simplices
  for (UnsignedInteger i = 0; i < simplicesSize; ++i)
  {
    Sample data(4, 2);
    data[0] = vertices_[simplices_[i][0]];
    data[1] = vertices_[simplices_[i][1]];
    data[2] = vertices_[simplices_[i][2]];
    data[3] = vertices_[simplices_[i][0]];
    Curve simplex(data);
    simplex.setColor("blue");
    if (i == 0) simplex.setLegend(String(OSS() << simplicesSize << " element" << (simplicesSize > 1 ? "s" : "")));
    graph.add(simplex);
  }
  graph.add(vertices);
  return graph;
}

Graph Mesh::draw3D(const Bool drawEdge,
                   const Scalar thetaX,
                   const Scalar thetaY,
                   const Scalar thetaZ,
                   const Bool shading,
                   const Scalar rho) const
{
  SquareMatrix R(3);
  const Scalar sinThetaX = sin(thetaX);
  const Scalar cosThetaX = cos(thetaX);
  const Scalar sinThetaY = sin(thetaY);
  const Scalar cosThetaY = cos(thetaY);
  const Scalar sinThetaZ = sin(thetaZ);
  const Scalar cosThetaZ = cos(thetaZ);
  R(0, 0) =  cosThetaY * cosThetaZ;
  R(1, 0) = -cosThetaY * sinThetaZ;
  R(2, 0) =  sinThetaY;
  R(0, 1) =  cosThetaX * sinThetaZ + sinThetaX * sinThetaY * cosThetaZ;
  R(1, 1) =  cosThetaX * cosThetaZ - sinThetaX * sinThetaY * sinThetaZ;
  R(2, 1) = -sinThetaX * cosThetaY;
  R(0, 2) =  sinThetaX * sinThetaZ - cosThetaX * sinThetaY * cosThetaZ;
  R(1, 2) =  sinThetaX * cosThetaZ + cosThetaX * sinThetaY * sinThetaZ;
  R(2, 2) =  cosThetaX * cosThetaY;
  return draw3D(drawEdge, R, shading, rho);
}

namespace
{
// Check whether a face of a simplex is inner or on a boundary.  Arguments must be sorted.
Bool isInnerFace(const Indices & simplices0, const Indices & simplices1, const Indices & simplices2)
{
  std::vector<UnsignedInteger> common01;
  std::set_intersection(simplices0.begin(), simplices0.end(), simplices1.begin(), simplices1.end(), std::back_inserter(common01));
  if (common01.size() < 2) return false;
  std::vector<UnsignedInteger> common012;
  std::set_intersection(simplices2.begin(), simplices2.end(), common01.begin(), common01.end(), std::back_inserter(common012));
  return common012.size() > 1;
}

// Check whether a face of a simplex is oriented toward the front or back
Bool isVisible(const Point & visuVertex0, const Point & visuVertex1, const Point & visuVertex2)
{
  return (visuVertex1[0] - visuVertex0[0]) * (visuVertex2[1] - visuVertex0[1]) <= (visuVertex1[1] - visuVertex0[1]) * (visuVertex2[0] - visuVertex0[0]);
}
} // anonymous namespace

Graph Mesh::draw3D(const Bool drawEdge,
                   const SquareMatrix & rotation,
                   const Bool shading,
                   const Scalar rho) const
{
  checkValidity();
  // First, check if the matrix is a rotation matrix of R^3
  if (rotation.getDimension() != 3) throw InvalidArgumentException(HERE) << "Error: the matrix is not a 3d square matrix.";
  if (Point((rotation * rotation.transpose() - IdentityMatrix(3)).getImplementation()).norm() > 1e-5) throw InvalidArgumentException(HERE) << "Error: the matrix is not a rotation matrix.";
  const UnsignedInteger verticesSize = getVerticesNumber();
  const UnsignedInteger simplicesSize = getSimplicesNumber();
  if (verticesSize == 0) throw InvalidArgumentException(HERE) << "Error: cannot draw a mesh with no vertex or no simplex.";
  // We use a basic Painter algorithm for the visualization
  // Second, transform the vertices if needed
  const Bool noRotation = rotation.isDiagonal();
  const Point center(noRotation ? Point(3) : vertices_.computeMean());
  const Sample visuVertices(noRotation ? vertices_ : rotation.getImplementation()->genSampleProd(vertices_ - center, true, false, 'R') + center);

  // Third, split all the simplices into triangles and compute their mean depth
  Collection< std::pair<Scalar, Indices> > trianglesAndDepth(4 * simplicesSize);
  UnsignedInteger triangleIndex = 0;
  Indices triangle(3);
  if (verticesToSimplices_.getSize() == 0) (void) getVerticesToSimplicesMap();
  const Bool backfaceCulling = ResourceMap::GetAsBool("Mesh-BackfaceCulling");
  for (UnsignedInteger i = 0; i < simplicesSize; ++i)
  {
    const UnsignedInteger i0 = simplices_[i][0];
    const UnsignedInteger i1 = simplices_[i][1];
    const UnsignedInteger i2 = simplices_[i][2];
    const UnsignedInteger i3 = simplices_[i][3];
    const Indices simplicesVertex0(verticesToSimplices_[i0]);
    const Indices simplicesVertex1(verticesToSimplices_[i1]);
    const Indices simplicesVertex2(verticesToSimplices_[i2]);
    const Indices simplicesVertex3(verticesToSimplices_[i3]);
    const Point visuVertex0(visuVertices[i0]);
    const Point visuVertex1(visuVertices[i1]);
    const Point visuVertex2(visuVertices[i2]);
    const Point visuVertex3(visuVertices[i3]);
    // First face: AB=p0p1, AC=p0p2.
    if (((!backfaceCulling) || isVisible(visuVertex0, visuVertex1, visuVertex2)) && (!isInnerFace(simplicesVertex0, simplicesVertex1, simplicesVertex2)))
    {
      triangle[0] = i0;
      triangle[1] = i1;
      triangle[2] = i2;
      trianglesAndDepth[triangleIndex].first = visuVertices(i0, 2) + visuVertices(i1, 2) + visuVertices(i2, 2);
      trianglesAndDepth[triangleIndex].second = triangle;
      ++triangleIndex;
    }

    // Second face: AB=p0p2, AC=p0p3.
    if (((!backfaceCulling) || isVisible(visuVertex0, visuVertex2, visuVertex3)) && (!isInnerFace(simplicesVertex0, simplicesVertex2, simplicesVertex3)))
    {
      triangle[0] = i0;
      triangle[1] = i2;
      triangle[2] = i3;
      trianglesAndDepth[triangleIndex].first = visuVertices(i0, 2) + visuVertices(i2, 2) + visuVertices(i3, 2);
      trianglesAndDepth[triangleIndex].second = triangle;
      ++triangleIndex;
    }

    // Third face: AB=p0p3, AC=p0p1.
    if (((!backfaceCulling) || isVisible(visuVertex0, visuVertex3, visuVertex1)) && (!isInnerFace(simplicesVertex0, simplicesVertex3, simplicesVertex1)))
    {
      triangle[0] = i0;
      triangle[1] = i3;
      triangle[2] = i1;
      trianglesAndDepth[triangleIndex].first = visuVertices(i0, 2) + visuVertices(i3, 2) + visuVertices(i1, 2);
      trianglesAndDepth[triangleIndex].second = triangle;
      ++triangleIndex;
    }

    // Fourth face: AB=p1p3, AC=p1p2.
    if (((!backfaceCulling) || isVisible(visuVertex1, visuVertex3, visuVertex2)) && (!isInnerFace(simplicesVertex1, simplicesVertex3, simplicesVertex2)))
    {
      triangle[0] = i1;
      triangle[1] = i3;
      triangle[2] = i2;
      trianglesAndDepth[triangleIndex].first = visuVertices(i1, 2) + visuVertices(i3, 2) + visuVertices(i2, 2);
      trianglesAndDepth[triangleIndex].second = triangle;
      ++triangleIndex;
    }
  }
  trianglesAndDepth.resize(triangleIndex);

  // Fourth, draw the triangles in decreasing depth
  Graph graph(String(OSS() << "Mesh " << getName()), "x", "y", true, "topright");
  std::sort(trianglesAndDepth.begin(), trianglesAndDepth.end());
  Scalar clippedRho = std::min(1.0, std::max(0.0, rho));
  if (rho != clippedRho) LOGWARN(OSS() << "The shrinking factor must be in (0,1), here rho=" << rho);
  Sample face(3, 2);
  Sample data;
  Description palette;
  if (!drawEdge)
  {
    data = Sample(3 * trianglesAndDepth.getSize(), 2);
    palette = Description(trianglesAndDepth.getSize());
  }
  UnsignedInteger base = 0;
  UnsignedInteger index = 0;

  const Scalar kSpecular = ResourceMap::GetAsScalar("Mesh-SpecularFactor");
  const Scalar kDiffuse = ResourceMap::GetAsScalar("Mesh-DiffuseFactor");
  const Scalar kAmbient = ResourceMap::GetAsScalar("Mesh-AmbientFactor");
  const Scalar shininess = ResourceMap::GetAsScalar("Mesh-Shininess");

  const Scalar redAmbient = 1.0;
  const Scalar greenAmbient = 1.0;
  const Scalar blueAmbient = 0.0;
  Point Iambient(3);
  Iambient[0] = kAmbient * redAmbient;
  Iambient[1] = kAmbient * greenAmbient;
  Iambient[2] = kAmbient * blueAmbient;

  const Scalar redFace = 0.0;
  const Scalar greenFace = 0.0;
  const Scalar blueFace = 1.0;

  const Scalar redEdge = 1.0;
  const Scalar greenEdge = 0.0;
  const Scalar blueEdge = 0.0;

  const Scalar redLight = 1.0;
  const Scalar greenLight = 1.0;
  const Scalar blueLight = 1.0;
  Point Ilight(3);

  // Will be modified if shading == true
  String faceColor = Drawable::ConvertFromRGB(redFace, greenFace, blueFace);
  String edgeColor = Drawable::ConvertFromRGB(redEdge, greenEdge, blueEdge);

  for (UnsignedInteger i = trianglesAndDepth.getSize(); i > 0; --i)
  {
    const UnsignedInteger i0 = trianglesAndDepth[i - 1].second[0];
    const UnsignedInteger i1 = trianglesAndDepth[i - 1].second[1];
    const UnsignedInteger i2 = trianglesAndDepth[i - 1].second[2];
    if (clippedRho < 1.0)
    {
      const Point center((visuVertices[i0] + visuVertices[i1] + visuVertices[i2]) / 3.0);
      face(0, 0) = center[0];
      face(0, 1) = center[1];
      face(1, 0) = center[0];
      face(1, 1) = center[1];
      face(2, 0) = center[0];
      face(2, 1) = center[1];
      if (clippedRho > 0.0)
      {
        face(0, 0) += clippedRho * (visuVertices(i0, 0) - center[0]);
        face(0, 1) += clippedRho * (visuVertices(i0, 1) - center[1]);
        face(1, 0) += clippedRho * (visuVertices(i1, 0) - center[0]);
        face(1, 1) += clippedRho * (visuVertices(i1, 1) - center[1]);
        face(2, 0) += clippedRho * (visuVertices(i2, 0) - center[0]);
        face(2, 1) += clippedRho * (visuVertices(i2, 1) - center[1]);
      }
    }
    else
    {
      face(0, 0) = visuVertices(i0, 0);
      face(0, 1) = visuVertices(i0, 1);
      face(1, 0) = visuVertices(i1, 0);
      face(1, 1) = visuVertices(i1, 1);
      face(2, 0) = visuVertices(i2, 0);
      face(2, 1) = visuVertices(i2, 1);
    }

    if (shading)
    {
      // The light source is behind the observer
      const Point ab(visuVertices[i1] - visuVertices[i0]);
      const Point ac(visuVertices[i2] - visuVertices[i0]);
      Point N(3);
      // The normal is vect(ab, ac)
      N[0] = ab[1] * ac[2] - ab[2] * ac[1];
      N[1] = ab[2] * ac[0] - ab[0] * ac[2];
      N[2] = ab[0] * ac[1] - ab[1] * ac[0];
      N /= N.norm();
      // Flip the normal if it is pointing backward
      if (N[2] < 0.0) N *= -1.0;
      const Scalar cosTheta = N[2];
      // R is a unit vector by construction
      Point R(N * (2.0 * cosTheta));
      R[2] -= 1.0;
      const Scalar cosPhi = std::abs(R[2]);
      const Scalar Idiffuse = kDiffuse * cosTheta;
      const Scalar Ispecular = kSpecular * pow(cosPhi, shininess);
      Point Ilight(3);
      Ilight[0] = Ispecular * redLight;
      Ilight[1] = Ispecular * greenLight;
      Ilight[2] = Ispecular * blueLight;
      // Face color using Phong model
      faceColor = Drawable::ConvertFromRGB(Iambient[0] + Idiffuse * redFace + Ilight[0], Iambient[1] + Idiffuse * greenFace + Ilight[1], Iambient[2] + Idiffuse * blueFace + Ilight[2]);
      edgeColor = Drawable::ConvertFromRGB(Iambient[0] + Idiffuse * redEdge + Ilight[0], Iambient[1] + Idiffuse * greenEdge + Ilight[1], Iambient[2] + Idiffuse * blueEdge + Ilight[2]);
    } // shading
    if (drawEdge)
    {
      Polygon triangle(face);
      triangle.setColor(faceColor);
      triangle.setEdgeColor(edgeColor);
      graph.add(triangle);
    } // drawEdge
    else
    {
      data(base, 0) = face(0, 0);
      data(base, 1) = face(0, 1);
      data(base + 1, 0) = face(1, 0);
      data(base + 1, 1) = face(1, 1);
      data(base + 2, 0) = face(2, 0);
      data(base + 2, 1) = face(2, 1);
      base += 3;
      palette[index] = faceColor;
      ++index;
    } // !drawEdge
  }
  if (!drawEdge) graph.add(PolygonArray(data, 3, palette));
  return graph;
}

/* Import mesh from FreeFem 2D mesh files */
Mesh Mesh::ImportFromMSHFile(const String & fileName)
{
  std::ifstream file(fileName.c_str(), std::ios::in);
  if (!file) throw FileNotFoundException(HERE) << "Error: can't open file " << fileName;
  // Bording case: empty file
  if (file.eof())
  {
    Log::Info(OSS() << "File " << fileName << " is empty.");
    return Mesh();
  }
  // First, the header: it is made of 3 integers, the number of vertices, the number of simplices and the number of elements on the boundary, currently not used by OT
  UnsignedInteger verticesNumber = 0;
  UnsignedInteger simplicesNumber = 0;
  UnsignedInteger scratch = 0;
  file >> verticesNumber;
  file >> simplicesNumber;
  file >> scratch;
  LOGINFO(OSS() << "Number of vertices=" << verticesNumber << ", number of simplices=" << simplicesNumber);
  // Parse the vertices
  Sample vertices(verticesNumber, 2);
  for (UnsignedInteger i = 0; i < verticesNumber; ++i)
  {
    file >> vertices(i, 0);
    file >> vertices(i, 1);
    file >> scratch;
    LOGINFO(OSS() << "vertex " << i << "=" << vertices[i]);
  }
  // Parse the simplices
  IndicesCollection simplices(simplicesNumber, Indices(3));
  for (UnsignedInteger i = 0; i < simplicesNumber; ++i)
  {
    file >> simplices[i][0];
    file >> simplices[i][1];
    file >> simplices[i][2];
    --simplices[i][0];
    --simplices[i][1];
    --simplices[i][2];
    file >> scratch;
    LOGINFO(OSS() << "simplex " << i << "=" << simplices[i]);
  }
  file.close();
  return Mesh(vertices, simplices);
}

/* VTK export */
String Mesh::streamToVTKFormat() const
{
  return streamToVTKFormat(simplices_);
}

String Mesh::streamToVTKFormat(const IndicesCollection & simplices) const
{
  if (dimension_ > 3) throw InvalidDimensionException(HERE) << "Error: cannot export a mesh of dimension=" << dimension_ << " into the VTK format. Maximum dimension is 3.";
  const UnsignedInteger oldPrecision = PlatformInfo::GetNumericalPrecision();
  PlatformInfo::SetNumericalPrecision(16);
  OSS oss;
  // First, the file version and identifier
  oss << "# vtk DataFile Version 3.0\n";
  // Second, the header
  oss << getName() << "\n";
  // Third, the format
  oss << "ASCII\n";
  oss << "\n";
  // Fourth, the data set
  oss << "DATASET UNSTRUCTURED_GRID\n";
  // Fifth, the geometrical and topological data
  // The vertices
  const UnsignedInteger numVertices = getVerticesNumber();
  oss << "POINTS " << numVertices << " float\n";
  for (UnsignedInteger i = 0; i < numVertices; ++i)
  {
    String separator("");
    for (UnsignedInteger j = 0; j < dimension_; ++j, separator = " ")
      oss << separator << vertices_(i, j);
    for (UnsignedInteger j = dimension_; j < 3; ++j)
      oss << separator << "0.0";
    oss << "\n";
  }
  // The simplices
  oss << "\n";
  const UnsignedInteger numSimplices = simplices.getSize();
  // If no simplex, assume that it is a cloud of points
  if (numSimplices == 0)
  {
    oss << "CELLS " << numVertices << " " << 2 * numVertices << "\n";
    for (UnsignedInteger i = 0; i < numVertices; ++i) oss << "1 " << i << "\n";
    oss << "\n";
    oss << "CELL_TYPES " << numVertices << "\n";
    for (UnsignedInteger i = 0; i < numVertices; ++i) oss << "1\n";
    PlatformInfo::SetNumericalPrecision(oldPrecision);
    return oss;
  }
  // There is at least on simplex. Assume homogeneous simplices,
  // ie all the simplices are of the same kind as the first one
  UnsignedInteger verticesPerSimplex = 1;
  UnsignedInteger lastIndex = simplices[0][0];
  while ((verticesPerSimplex <= dimension_) && (simplices[0][verticesPerSimplex] != lastIndex))
  {
    lastIndex = simplices[0][verticesPerSimplex];
    ++verticesPerSimplex;
  }
  oss << "CELLS " << numSimplices << " " << (verticesPerSimplex + 1) * numSimplices << "\n";
  for (UnsignedInteger i = 0; i < numSimplices; ++i)
  {
    oss << verticesPerSimplex;
    for (UnsignedInteger j = 0; j < verticesPerSimplex; ++j) oss << " " << simplices[i][j];
    oss << "\n";
  }
  oss << "\n";
  // If no simplices, assume vertices type
  oss << "CELL_TYPES " << numSimplices << "\n";
  UnsignedInteger cellType = 0;
  // Cell type is:
  // 1 for vertex
  // 3 for line
  // 5 for triangle
  // 10 for tetrahedron
  if (verticesPerSimplex == 1) cellType = 1;
  if (verticesPerSimplex == 2) cellType = 3;
  if (verticesPerSimplex == 3) cellType = 5;
  if (verticesPerSimplex == 4) cellType = 10;
  for (UnsignedInteger i = 0; i < numSimplices; ++i) oss << cellType << "\n";
  PlatformInfo::SetNumericalPrecision(oldPrecision);
  return oss;
}

void Mesh::exportToVTKFile(const String & fileName) const
{
  exportToVTKFile(fileName, simplices_);
}

void Mesh::exportToVTKFile(const String & fileName,
			   const IndicesCollection & simplices) const
{
  std::ofstream file(fileName.c_str(), std::ios::out);
  if (!file) throw FileNotFoundException(HERE) << "Error: can't open file " << fileName;
  const String content(streamToVTKFormat(simplices));
  file << content;
  file.close();
}

/* Method save() stores the object through the StorageManager */
void Mesh::save(Advocate & adv) const
{
  DomainImplementation::save(adv);
  adv.saveAttribute("isAlreadyComputedVolume_", isAlreadyComputedVolume_);
  adv.saveAttribute("volume_", volume_);
  adv.saveAttribute("vertices_", vertices_);
  adv.saveAttribute("simplices_", simplices_);
  adv.saveAttribute("tree_", tree_);
  adv.saveAttribute("verticesToSimplices_", verticesToSimplices_);
  adv.saveAttribute("lowerBoundingBoxSimplices_", lowerBoundingBoxSimplices_);
  adv.saveAttribute("upperBoundingBoxSimplices_", upperBoundingBoxSimplices_);
}

/* Method load() reloads the object from the StorageManager */
void Mesh::load(Advocate & adv)
{
  DomainImplementation::load(adv);
  adv.loadAttribute("isAlreadyComputedVolume_", isAlreadyComputedVolume_);
  adv.loadAttribute("volume_", volume_);
  adv.loadAttribute("vertices_", vertices_);
  adv.loadAttribute("simplices_", simplices_);
  adv.loadAttribute("tree_", tree_);
  adv.loadAttribute("verticesToSimplices_", verticesToSimplices_);
  adv.loadAttribute("lowerBoundingBoxSimplices_", lowerBoundingBoxSimplices_);
  adv.loadAttribute("upperBoundingBoxSimplices_", upperBoundingBoxSimplices_);
}

END_NAMESPACE_OPENTURNS
