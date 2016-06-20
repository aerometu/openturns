//                                               -*- C++ -*-
/**
 *  @file  HMatrixFactory.hxx
 *  @brief This file supplies support for HMat
 *
 *  Copyright 2005-2016 Airbus-EDF-IMACS-Phimeca
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
#ifndef OPENTURNS_HMATRIXFACTORY_HXX
#define OPENTURNS_HMATRIXFACTORY_HXX

#include "openturns/OTprivate.hxx"
#include "openturns/PersistentObject.hxx"


BEGIN_NAMESPACE_OPENTURNS

// Forward declarations
class HMatrix;
class NumericalSample;

/**
 * @class HMatrixFactory
 *
 */

class OT_API HMatrixFactory
  : public PersistentObject
{
  CLASSNAME;

public:
  /** Default constructor */
  HMatrixFactory();

  /** Virtual constructor */
  virtual HMatrixFactory * clone() const;

  /** Method to create an HMatrix object */
  HMatrix build(const NumericalSample & sample, UnsignedInteger outputDimension, Bool symmetric);

  /** Tell whether HMat support is available */
  static Bool IsAvailable();

  /** String converter */
  virtual String __repr__() const;

  /** Method save() stores the object through the StorageManager */
  virtual void save(Advocate & adv) const;

  /** Method load() reloads the object from the StorageManager */
  virtual void load(Advocate & adv);
}; /* end class HMatrixFactory */


END_NAMESPACE_OPENTURNS

#endif /* OPENTURNS_HMATRIXFACTORY_HXX */