#include "WaveformsAtAPointFT.hpp"

#include "NoiseCurves.hpp"
#include "Utilities.hpp"
#include "fft.hpp"
#include "Errors.hpp"

#include "Waveforms.hpp"
#include <complex>

namespace WU = WaveformUtilities;
using std::vector;
using std::cerr;
using std::endl;
using std::ios_base;
using std::complex;
using GWFrames::operator*;
using GWFrames::operator-;

namespace {

  double sqr(const double& a) { return a*a; }
  double cube(const double& a) { return a*a*a; }

  double BumpFunction(const double x, const double x0, const double x1) {
    if(x<=x0) { return 0.0; }
    if(x>=x1) { return 1.0; }
    const double t = (x-x0)/(x1-x0);
    return 1.0 / (1.0 + exp(1.0/t - 1.0/(1-t)));
  }

  // // Unused:
  // double DoubleSidedF(const unsigned int i, const unsigned int N, const double df) {
  //   if(i<N/2) { return i*df; }
  //   return i*df - N*df;
  // }

}

GWFrames::WaveformAtAPointFT::
WaveformAtAPointFT(const GWFrames::Waveform& W,
                   const double Dt,
                   const double Vartheta,
                   const double Varphi,
                   const double TotalMass,
                   const unsigned int WindowNCycles,
                   const double DetectorResponseAmp,
                   const double DetectorResponsePhase,
                   const unsigned int ExtraZeroPadPowers)
  : mDt(Dt), mVartheta(Vartheta), mVarphi(Varphi), mNormalized(false)
{
  cerr << "The old constructor of WaveformAtAPointFT has been disabled.\n"
       << "Please use the new constructor, which takes 'WindowEndTime'\n"
       << "instead of 'WindowNCycles' and takes 'Direction' instead of\n"
       << "'Vartheta' and 'Varphi'." << endl;
  throw(GWFrames_NotYetImplemented);
}

GWFrames::WaveformAtAPointFT::
WaveformAtAPointFT(const GWFrames::Waveform& W,
                   const double Dt,
                   const std::vector<double>& Direction,
                   const double TotalMass,
                   const double WindowBeginTime,
                   const double WindowEndTime,
                   const double DetectorResponseAmp,
                   const double DetectorResponsePhase,
                   const unsigned int ExtraZeroPadPowers)
  : mDt(Dt), mNormalized(false)
{
  if(Direction.size() != 2) {
    cerr << "Direction should be vector (theta,phi) of size 2, not size "
         << Direction.size() << endl;
    throw(GWFrames_VectorSizeMismatch);
  }
  mVartheta=Direction[0];
  mVarphi  =Direction[1];

  // Interpolate to an even time spacing dt whose size is the next power of 2
  // Then zero pad for additional powers of 2 if requested (may be needed for
  // more fine-grained control of time and phase offsets)
  const unsigned int N1 = (unsigned int)(std::floor((W.T().back()-W.T(0))/Dt));
  const unsigned int NPow = ceil(log2(N1)) + ExtraZeroPadPowers;
  const unsigned int N2 = (unsigned int)(std::pow(2.0, double(NPow)));
  vector<double> NewTimes(N2);
  for(unsigned int i=0; i<N2; ++i) {
    NewTimes[i] = W.T(0) + i*Dt;
  }

  // Sanity checks
  if(WindowBeginTime >= WindowEndTime) {
    cerr << "WindowEndTime " << WindowEndTime
         << " should be > WindowBeginTime " << WindowBeginTime << endl;
    throw(GWFrames_EmptyIntersection);
  }
  if(WindowEndTime < NewTimes[0] || WindowEndTime > NewTimes.back()) {
    cerr << "WindowEndTime " << WindowEndTime
         << " out of range [" << NewTimes[0] << "," << NewTimes.back()
         << "]" << endl;
    throw(GWFrames_EmptyIntersection);
  }
  if(WindowBeginTime < NewTimes[0] || WindowBeginTime > NewTimes.back()) {
    cerr << "WindowBeginTime " << WindowBeginTime
         << " out of range [" << NewTimes[0] << "," << NewTimes.back()
         << "]" << endl;
    throw(GWFrames_EmptyIntersection);
  }

  GWFrames::Waveform W2 = W.Interpolate(NewTimes, true);
  const vector<complex<double> > ComplexHData = W2.EvaluateAtPoint(mVartheta,
                                                                   mVarphi);

  // Construct initial real,imag H as a function of time
  vector<double> InitRealT(ComplexHData.size());
  vector<double> InitImagT(ComplexHData.size());
  for (size_t i=0; i<ComplexHData.size(); i++) {
    InitRealT[i] = ComplexHData[i].real();
    InitImagT[i] = ComplexHData[i].imag();
  }

  // Account for detector response in H as a function of time
  vector<double> RealT;
  if(DetectorResponsePhase != 0.0) {
    RealT = InitRealT*(DetectorResponseAmp*cos(DetectorResponsePhase))
      -InitImagT*(DetectorResponseAmp*sin(DetectorResponsePhase));
  } else if(DetectorResponseAmp != 1.0) {
    RealT = InitRealT*DetectorResponseAmp;
  } else {
    RealT = InitRealT;
  }

  // Window the data (NOTE: this zeros everything before WindowBeginTime)
  for(unsigned int j=0; j<NewTimes.size(); j++) {
    RealT[j] *= BumpFunction(NewTimes[j], WindowBeginTime, WindowEndTime);
    // For speed, break after WindowEndTime to avoid multiplying by 1
    if (NewTimes[j] >= WindowEndTime) { break; }
  }

  // Set up the frequency domain (in Hz)
  double Dt_dimensionful = 0.0;
  {
    // Conversion of time to seconds from geometric units

    // Note that SolarMass * G has been measured to much better accuracy
    // than either the solar mass (in kg) or G (in m^3 kg^-1 s^-2).
    // The value below comes from http://ssd.jpl.nasa.gov/?constants
    // and has an error bar of 8x10^9, i.e. 8 in the last digit.
    const double GMsol = 1.32712440018e20; // Solar mass * G, in m^3 s^-2

    // The value of c below is exact.  The length of the meter is defined
    // from this constant and the definition of the second.
    const double c    = 299792458;       // Units of m/s.

    const double TotalMassInSeconds = TotalMass * GMsol / cube(c);
    Dt_dimensionful = Dt * TotalMassInSeconds;
    const vector<double> PhysicalTimes = TotalMassInSeconds * NewTimes;
    mFreqs = WU::TimeToPositiveFrequencies(PhysicalTimes);
  }

  // Construct real,imag H as a function of frequency
  // The return from realdft needs to be multiplied by dt to correspond to the continuum FT
  WU::realdft(RealT);
  if (mFreqs.size() != RealT.size()/2+1) {
    cerr << "Time and frequency data size mismatch: "
         << mFreqs.size() << "," << RealT.size()/2+1 << endl;
    throw(GWFrames_VectorSizeMismatch);
  }
  mRealF.resize(mFreqs.size());
  mImagF.resize(mFreqs.size());
  for(unsigned int i=0; i<RealT.size()/2; ++i) {
    mRealF[i] = Dt_dimensionful*RealT[2*i];
    mImagF[i] = Dt_dimensionful*RealT[2*i+1];
  }
  // Sort out some funky storage
  mRealF.back() = 0.0; // RealT[1]; // just ignore data at the Nyquist frequency
  mImagF.back() = 0.0;
  mImagF[0] = 0.0;
}

namespace GWFrames {
  WaveformAtAPointFT& WaveformAtAPointFT::Normalize(const vector<double>& InversePSD)
  {
    if(mNormalized) { return *this; }
    const double snr = SNR(InversePSD);
    for (size_t i=0; i<mRealF.size(); i++) {
      mRealF[i] /= snr;
      mImagF[i] /= snr;
    }
    mNormalized = true;
    return *this;
  }

  WaveformAtAPointFT& WaveformAtAPointFT::Normalize(const std::string& Detector)
  {
    return Normalize(WU::InverseNoiseCurve(F(), Detector));
  }

  WaveformAtAPointFT& WaveformAtAPointFT::ZeroAbove(const double Frequency)
  {
    for(unsigned int f=0; f<NFreq(); ++f) {
      if(std::fabs(F(f)) > Frequency) {
        mRealF[f] = 0.0;
        mImagF[f] = 0.0;
      }
    }
    return *this;
  }

  // TODO: Assert same df, not just NFreq()
  double WaveformAtAPointFT::InnerProduct(const WaveformAtAPointFT& B,
                                          const vector<double>& InversePSD) const
  {
    if(NFreq() != B.NFreq()) {
      cerr << "\nthis->NFreq()=" << NFreq() << "\tB.NFreq()=" << B.NFreq() << endl;
      throw(GWFrames_VectorSizeMismatch);
    }
    if(NFreq() != InversePSD.size()) {
      cerr << "\nthis->NFreq()=" << NFreq() << "\tInversePSD.size()=" << InversePSD.size() << endl;
      throw(GWFrames_VectorSizeMismatch);
    }
    double InnerProduct = 0.0;
    for(unsigned int i=0; i<NFreq(); ++i) {
      InnerProduct += (Re(i)*B.Re(i)+Im(i)*B.Im(i))*InversePSD[i];
    }
    InnerProduct = 4*(F(1)-F(0))*InnerProduct; // Remember: single-sided frequency
    return InnerProduct;
  }

  vector<double> WaveformAtAPointFT::InversePSD(const std::string& Detector) const
  {
    return WU::InverseNoiseCurve(F(), Detector);
  }

  double WaveformAtAPointFT::SNR(const std::vector<double>& InversePSD) const
  {
    /// \param[in] InversePSD Noise spectrum
    if(NFreq() != InversePSD.size()) {
      cerr << "\nthis->NFreq()=" << NFreq() << "\tInversePSD.size()=" << InversePSD.size() << endl;
      throw(GWFrames_VectorSizeMismatch);
    }
    double SNRSquared = 0.0;
    for(unsigned int i=0; i<NFreq(); ++i) {
      SNRSquared += (Re(i)*Re(i)+Im(i)*Im(i))*InversePSD[i];
    }
    SNRSquared = 4*(F(1)-F(0))*SNRSquared; // Remember: single-sided frequency
    return std::sqrt(SNRSquared);
  }

  double WaveformAtAPointFT::SNR(const std::string& Detector) const
  {
    /// \param[in] Detector Noise spectrum from this detector
    return SNR(WU::InverseNoiseCurve(F(), Detector));
  }

  /// Compute the match between two WaveformAtAPointFT
  /// NOTE: in python, the values for timeOffset, phaseOffset, and match are
  /// also part of the return value (even though Match returns void), e.g.:
  ///   timeOffset, phaseOffset, match = Match(...)
  void WaveformAtAPointFT::Match(const WaveformAtAPointFT& B,
                                 const std::vector<double>& InversePSD,
                                 double& timeOffset, double& phaseOffset,
                                 double& match) const
  {
    /// \param[in] B WaveformAtAPointFT to compute match with
    /// \param[in] InversePSD Spectrum used to weight contributions by frequencies to match
    /// \param[out] timeOffset Time offset (in seconds) between the waveforms.
    ///             Sign is chosen so that in the time domain, *this(t)
    ///             corresponds to B(t-timeOffset), i.e. the waveform B
    ///             is shifted to the right by timeOffset.  Equivalently,
    ///             in the frequency domain, *this is compared to
    ///             B*exp(-2pi*i*f*timeOffset)
    /// \param[out] phaseOffset Phase offset used between the waveforms.
    ///             Sign is chosen so that in the time domain, Arg(*this)
    ///             corresponds to Arg(B)+phaseOffset.
    ///             Equivalently, in the frequency domain, *this is compared to
    ///             B*exp(i*phaseOffset)
    /// \param[out] match Match between the two waveforms
    const unsigned int n = NFreq(); // Only positive frequencies are stored in t
    const unsigned int N = 2*(n-1);  // But this is how many there really are
    if(!IsNormalized() || !B.IsNormalized()) {
      cerr << "\n\nWARNING!!! Matching non-normalized WaveformAtAPointFT objects. WARNING!!!\n" << endl;
    }
    if(n != B.NFreq() || n != InversePSD.size()) {
      cerr << "Waveform sizes, " << n << " and " << B.NFreq()
           << ", are not compatible with InversePSD size, " << InversePSD.size() << "." << endl;
      throw(GWFrames_VectorSizeMismatch);
    }
    const double eps = 1e-8;
    const double df = F(1)-F(0);
    const double df_B = B.F(1)-B.F(0);
    const double rel_diff_df = std::fabs(1 - df/df_B);
    if(rel_diff_df > eps) {
      cerr << "Waveform frequency steps, " << df << " and " << df_B
           << ", are not compatible in Match: rel_diff="<< rel_diff_df << endl;
      throw(GWFrames_VectorSizeMismatch);
    }
    // s1 s2* = (a1 + i b1) (a2 - i b2) = (a1 a2 + b1 b2) + i(b1 a2 - a1 b2)
    WU::WrapVecDoub data(2*N);
    for(unsigned int i=0; i<n; ++i) {
      data.real(i) = (Re(i)*B.Re(i)+Im(i)*B.Im(i))*InversePSD[i];
      data.imag(i) = (Im(i)*B.Re(i)-Re(i)*B.Im(i))*InversePSD[i];
    }
    idft(data);
    unsigned int maxi=0;
    double maxmag = std::sqrt(sqr(data.real(0)) + sqr(data.imag(0)));
    for(unsigned int i=1; i<N; ++i) {
      const double mag = std::sqrt(sqr(data.real(i)) + sqr(data.imag(i)));
      if(mag>maxmag) { maxmag = mag; maxi = int(i); }
    }
    // note: assumes N is even and N >= maxi
    timeOffset = (maxi<N/2 ? double(maxi)/(N*df) : -double(N-maxi)/(N*df));
    phaseOffset = atan2(data.imag(maxi), data.real(maxi))/2.0;
    /// The return from ifft is just the bare FFT sum, so we multiply by
    /// df to get the continuum-analog FT.  This is correct because the
    /// input data (re,im) are the continuum-analog data, rather than
    /// just the return from the bare FFT sum.  See, e.g., Eq. (A.33)
    /// [as opposed to Eq. (A.35)] of my (Mike Boyle's) thesis:
    /// <http://thesis.library.caltech.edu/143>.
    match = 4.0*df*maxmag;
    return;
  }

  /// Compute the match between two WaveformAtAPointFT
  /// NOTE: in python, the values for timeOffset, phaseOffset, and match are
  /// also part of the return value (even though Match returns void), e.g.:
  ///   timeOffset, phaseOffset, match = Match(...)
  void WaveformAtAPointFT::Match(const WaveformAtAPointFT& B, double& timeOffset,
                                 double& phaseOffset, double& match,
                                 const std::string& Detector) const
  {
    Match(B, WU::InverseNoiseCurve(F(), Detector), timeOffset, phaseOffset, match);
    return;
  }

  /// Compute the match between two WaveformAtAPointFT
  double WaveformAtAPointFT::Match(const WaveformAtAPointFT& B,
                                   const std::vector<double>& InversePSD) const
  {
    double match, timeOffset, phaseOffset;
    Match(B, InversePSD, timeOffset, phaseOffset, match);
    return match;
  }

  /// Compute the match between two WaveformAtAPointFT
  double WaveformAtAPointFT::Match(const WaveformAtAPointFT& B,
                                   const std::string& Detector) const
  {
    return Match(B, WU::InverseNoiseCurve(F(), Detector));
  }

}
