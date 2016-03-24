//                                               -*- C++ -*-
/**
 *  @brief This class implements the fourth order fixed-step Runge-Kutta ODE integrator
 *
 *  Copyright 2005-2015 Airbus-EDF-IMACS-Phimeca
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
#ifndef OPENTURNS_RUNGEKUTTA_HXX
#define OPENTURNS_RUNGEKUTTA_HXX

#include "ODESolverImplementation.hxx"

BEGIN_NAMESPACE_OPENTURNS

/**
 * @class RungeKutta
 *
 * Fourth order fixed-step Runge-Kutta ODE integrator
 */
class OT_API RungeKutta
  : public ODESolverImplementation
{
  CLASSNAME;
public:

  /** Default constructor */
  RungeKutta();

  /** Parameter constructor */
  explicit RungeKutta(const DynamicalFunction & transitionFunction);

  /** Virtual constructor */
  virtual RungeKutta * clone() const;

  /** String converter */
  virtual String __repr__() const;

  /** Perform cross-validation */
  NumericalSample solve(const NumericalPoint & initialState,
                        const NumericalPoint & timeGrid) const;

  /** Method save() stores the object through the StorageManager */
  void save(Advocate & adv) const;

  /** Method load() reloads the object from the StorageManager */
  void load(Advocate & adv);

private:
  /** Perform one step of the RungeKutta method */
  NumericalPoint computeStep(const NumericalScalar t,
                             const NumericalPoint & state,
                             const NumericalScalar h) const;

}; /* class RungeKutta */


END_NAMESPACE_OPENTURNS

#endif /* OPENTURNS_RUNGEKUTTA_HXX */