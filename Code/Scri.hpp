// Copyright (c) 2014, Michael Boyle
// See LICENSE file for details

#ifndef SCRI_HPP
#define SCRI_HPP

#include <vector>
#include <complex>
#include "Utilities.hpp"
#include "Quaternions.hpp"
#include "Waveforms.hpp"

namespace GWFrames {

  // This header defines the basic objects `Modes` and `DataGrid`.
  // `SliceOfScri<D>` contains a set of either `Modes` or `DataGrid`
  // objects.  `SliceModes` and `SliceGrid` are specific instances of
  // this template; `SliceModes` has some extras
  //

  class Modes; // Forward declaration for DataGrid constructor
  class ScriFunctor { public: virtual double operator()(const Quaternions::Quaternion&) const { return 0.0; } };
  Quaternions::Quaternion Boost(GWFrames::ThreeVector v, GWFrames::ThreeVector n);

  class DataGrid {
    /// This object holds complex spin-weighted data on the sphere in
    /// an equi-angular representation.  That is, given integers
    /// n_theta and n_phi, the data are recorded on a "rectangular"
    /// grid with n_theta*n_phi points, including n_phi points at each
    /// of the poles.  The purpose of these objects is primarily to
    /// serve as a computational tool for pointwise multiplication.
    /// Otherwise, `Modes` is expected to be the preferable
    /// representation.
  private: // Data
    int s;
    int n_theta;
    int n_phi;
    std::vector<std::complex<double> > data;
  public: // Constructors
    DataGrid(const int size=0) : s(0), n_theta(std::sqrt(size)), n_phi(std::sqrt(size)), data(size) { }
    DataGrid(const DataGrid& A) : s(A.s), n_theta(A.n_theta), n_phi(A.n_phi), data(A.data) { }
    DataGrid(const int Spin, const int N_theta, const int N_phi, const std::vector<std::complex<double> >& D);
    explicit DataGrid(Modes M, const int N_theta=0, const int N_phi=0); // Can't be const& because of spinsfast design
    DataGrid(const Modes& M, const GWFrames::ThreeVector& v, const int N_theta=0, const int N_phi=0);
    DataGrid(const int Spin, const int N_theta, const int N_phi, const GWFrames::ThreeVector& v, const ScriFunctor& f);
  public: // Modification
    inline DataGrid& SetSpin(const int ess) { s=ess; return *this; }
    inline DataGrid& SetNTheta(const int N_theta) { n_theta=N_theta; return *this; }
    inline DataGrid& SetNPhi(const int N_phi) { n_phi=N_phi; return *this; }
  public: // Access and operators
    inline unsigned int size() const { return data.size(); }
    inline int Spin() const { return s; }
    inline int N_theta() const { return n_theta; }
    inline int N_phi() const { return n_phi; }
    inline const std::complex<double>& operator[](const unsigned int i) const { return data[i]; }
    inline std::complex<double>& operator[](const unsigned int i) { return data[i]; }
    inline std::vector<std::complex<double> > Data() const { return data; }
    DataGrid operator*(const DataGrid&) const;
    DataGrid operator/(const DataGrid&) const;
    DataGrid operator+(const DataGrid&) const;
    DataGrid operator-(const DataGrid&) const;
    DataGrid pow(const int p) const;
  }; // class DataGrid
  DataGrid operator*(const double& a, const DataGrid& b);
  DataGrid operator/(const double& a, const DataGrid& b);
  DataGrid operator+(const double& a, const DataGrid& b);
  DataGrid operator-(const double& a, const DataGrid& b);
  DataGrid ConformalFactorGrid(const GWFrames::ThreeVector& v, const int n_theta, const int n_phi);
  DataGrid InverseConformalFactorGrid(const GWFrames::ThreeVector& v, const int n_theta, const int n_phi);
  DataGrid InverseConformalFactorBoostedGrid(const GWFrames::ThreeVector& v, const int n_theta, const int n_phi);


  class Modes {
    /// This object holds complex spin-weighted data on the sphere in
    /// a spin-weighted spherical-harmonic mode representation.  All
    /// modes are present, even for \f$\ell<|s|\f$.  The modes are
    /// also assumed to be stored in order, as \f$(\ell,m) = (0,0),
    /// (1,-1), (1,0), (1,1), (2,-2), \ldots\f$.
  private: // Data
    int s;
    int ellMax;
    std::vector<std::complex<double> > data;
  public: // Constructors
    Modes(const int size=0): s(0), ellMax(0), data(size) { }
    Modes(const Modes& A) : s(A.s), ellMax(A.ellMax), data(A.data) { }
    Modes(const int spin, const std::vector<std::complex<double> >& Data);
    explicit Modes(DataGrid D, const int L=-1); // Can't be const& because of spinsfast design
    Modes& operator=(const Modes& B);
  public: // Modification
    inline Modes& SetSpin(const int ess) { s=ess; return *this; }
    inline Modes& SetEllMax(const int ell) { ellMax=ell; return *this; }
  public: // Access
    inline unsigned int size() const { return data.size(); }
    inline int Spin() const { return s; }
    inline int EllMax() const { return ellMax; }
    inline std::complex<double> operator[](const unsigned int i) const { return data[i]; }
    inline std::complex<double>& operator[](const unsigned int i) { return data[i]; }
    inline std::vector<std::complex<double> > Data() const { return data; }
  public: // Operations
    Modes pow(const int p) const { return Modes(DataGrid(*this, 2*EllMax()*p+1, 2*EllMax()*p+1).pow(p)); }
    Modes bar() const;
    Modes operator*(const Modes& M) const;
    Modes operator/(const Modes& M) const;
    Modes operator+(const Modes& M) const;
    Modes operator-(const Modes& M) const;
    Modes edth() const;
    Modes edthbar() const;
    Modes edth2edthbar2() const;
    std::complex<double> EvaluateAtPoint(const double vartheta, const double varphi) const;
    std::complex<double> EvaluateAtPoint(const Quaternions::Quaternion& R) const;
  }; // class Modes
  GWFrames::ThreeVector vFromOneOverK(const GWFrames::Modes& OneOverK);


  template <class D>
  class SliceOfScri {
    /// This class holds all the necessary objects needed to
    /// understand the geometry of a given slice of null infinity.
  public: // Data
    D psi0, psi1, psi2, psi3, psi4, sigma, sigmadot; // complex mode data for these objects on this slice
  public: // Constructors
    SliceOfScri(const int size=0);
    SliceOfScri(const SliceOfScri& S) : psi0(S.psi0), psi1(S.psi1), psi2(S.psi2), psi3(S.psi3), psi4(S.psi4), sigma(S.sigma), sigmadot(S.sigmadot) { }
  public: //Access
    inline const D& operator[](const unsigned int i) const {
      if(i==0) { return psi0; }
      else if(i==1) { return psi1; }
      else if(i==2) { return psi2; }
      else if(i==3) { return psi3; }
      else if(i==4) { return psi4; }
      else if(i==5) { return sigma; }
      else if(i==6) { return sigmadot; }
      else { std::cerr << "\n\n" << __FILE__ << ":" << __LINE__ << "\nError: (i=" << i << ")>6\n" << std::endl; throw(GWFrames_IndexOutOfBounds); }
    }
    inline D& operator[](const unsigned int i) {
      if(i==0) { return psi0; }
      else if(i==1) { return psi1; }
      else if(i==2) { return psi2; }
      else if(i==3) { return psi3; }
      else if(i==4) { return psi4; }
      else if(i==5) { return sigma; }
      else if(i==6) { return sigmadot; }
      else { std::cerr << "\n\n" << __FILE__ << ":" << __LINE__ << "\nError: (i=" << i << ")>6\n" << std::endl; throw(GWFrames_IndexOutOfBounds); }
    }
  }; // class SliceOfScri

  typedef SliceOfScri<DataGrid> SliceGrid;


  class SliceModes : public SliceOfScri<Modes> {
  public:
    // Constructors
    SliceModes(const int ellMax=0);
    SliceModes(const SliceModes& S) : SliceOfScri<Modes>(S) { }
    // Useful quantities
    int EllMax() const;
    double Mass() const;
    GWFrames::FourVector FourMomentum() const;
    Modes SuperMomentum() const;
    // Transformations
    SliceGrid BMSTransformationOnSlice(const double u, const GWFrames::ThreeVector& v, const GWFrames::Modes& delta) const;
    // Moreschi algorithm
    void MoreschiIteration(GWFrames::Modes& OneOverK_ip1, GWFrames::Modes& delta_ip1) const;
  }; // class SliceModes


  typedef std::vector<DataGrid> SliceOfScriGrids;


  class Scri {
    /// A `Scri` object contains all the information needed to express
    /// the asymptotic geometry of null infinity, and symmetry
    /// transformations thereof.  This geometry is encoded in the
    /// Newman--Penrose curvature scalars \f$\psi_0, \ldots, \psi_4\f$
    /// and the complex shear \f$\sigma\f$ of outgoing null rays
    /// (strain in gravitational-wave detectors).  The general
    /// symmetry transformation is an element of the
    /// Bondi--Metzner--Sachs (BMS) group, which transforms the data
    /// contained by `Scri` among itself.
  private: // Member data
    std::vector<double> t;
    std::vector<SliceModes> slices;
  public: // Constructor
    Scri(const GWFrames::Waveform& psi0, const GWFrames::Waveform& psi1,
         const GWFrames::Waveform& psi2, const GWFrames::Waveform& psi3,
         const GWFrames::Waveform& psi4, const GWFrames::Waveform& sigma);
    Scri(const Scri& S) : t(S.t), slices(S.slices) { }
  public: // Member functions
    // Transformations
    SliceModes BMSTransformation(const double& u0, const GWFrames::ThreeVector& v, const GWFrames::Modes& delta) const;
    // Access
    inline int NTimes() const { return t.size(); }
    inline const std::vector<double> T() const { return t; }
    inline const SliceModes operator[](const unsigned int i) const { return slices[i]; }
    inline SliceModes& operator[](const unsigned int i) { return slices[i]; }
  }; // class Scri


  class SuperMomenta {
  private:
    std::vector<double> t;
    std::vector<Modes> Psi;
  public:
    // Constructors
    SuperMomenta(const unsigned int size) : t(size), Psi(size) { }
    SuperMomenta(const SuperMomenta& S) : t(S.t), Psi(S.Psi) { }
    SuperMomenta(const std::vector<double>& T, const std::vector<Modes>& psi) : t(T), Psi(psi) { }
    SuperMomenta(const Scri& scri);
    // Access
    inline int NTimes() const { return t.size(); }
    inline const std::vector<double> T() const { return t; }
    inline const Modes operator[](const unsigned int i) const { return Psi[i]; }
    inline Modes& operator[](const unsigned int i) { return Psi[i]; }
    // Transformations
    Modes BMSTransform(const GWFrames::Modes& OneOverK, const GWFrames::Modes& delta) const;
    void MoreschiIteration(GWFrames::Modes& OneOverK, GWFrames::Modes& delta) const;
  }; // class SuperMomenta


} // namespace GWFrames

#endif // SCRI_HPP
