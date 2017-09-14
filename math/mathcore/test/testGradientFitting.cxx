// @(#)root/test:$Id$
// Author: Alejandro García Montoro 08/2017

#include "Fit/BinData.h"
#include "Fit/UnBinData.h"
#include "Fit/Fitter.h"
#include "HFitInterface.h"
#include "TH2.h"
#include "TF2.h"
#include "TRandom.h"

#include "gtest/gtest.h"

#include <iostream>
#include <string>


// Gradient 2D function
template <class T>
class GradFunc2D : public ROOT::Math::IParamMultiGradFunctionTempl<T> {
public:
   void SetParameters(const double *p) {
      std::copy(p, p + NPar(), fParameters);
      // compute integral in interval [0,1][0,1]
      fIntegral = Integral(p);
   }

   const double *Parameters() const { return fParameters; }

   ROOT::Math::IBaseFunctionMultiDimTempl<T> *Clone() const
   {
      GradFunc2D<T> *f = new GradFunc2D<T>();
      f->SetParameters(fParameters);
      return f;
   }

   unsigned int NDim() const { return 2; }

   unsigned int NPar() const { return 5; }

   void ParameterGradient(const T *x, const double * p, T *grad) const
   {
      if (p == nullptr) {
         ParameterGradient(x, fParameters, grad);
         return; 
      }
      T xx = (1. - x[0] );
      T yy = (1. - x[1] ); 
      T fval = FVal(x,p);
      grad[0] =  fval / fIntegral;  
      grad[1] =  p[0] * ( xx / fIntegral - fval / (2. * fIntegral * fIntegral ) );
      grad[2] =  p[0] * ( xx * xx / fIntegral -  fval / (3. * fIntegral * fIntegral ) );
      grad[3] =  p[0] * ( yy / fIntegral - fval / (2. * fIntegral * fIntegral ) );
      grad[4] =  p[0] * ( yy * yy / fIntegral -  fval / (3. * fIntegral * fIntegral ) );
   }

   // return integral in interval {0,1}{0,1}
   double Integral(const double * p)
   {
      return 1. +  (p[1] + p[3] )/ 2. + (p[2] + p[4] )/ 3.; 
   }

private:

   T FVal(const T * x, const double *p) const
   {
      // use a function based on Bernstein polynomial which have easy normalization
      T xx = (1. - x[0] );
      T yy = (1. - x[1] ); 
      T fval =  1. + p[1] * xx + p[2] * xx * xx + p[3] * yy + p[4] * yy * yy;
      return fval; 
   }

   T DoEvalPar(const T *x, const double *p) const
   {
      if (p == nullptr) 
         return DoEvalPar(x, fParameters); 
      return p[0] * FVal(x,p) / fIntegral; 
   }

   T DoParameterDerivative(const T *x, const double *p, unsigned int ipar) const
   {
      std::vector<T> grad(NPar());
      ParameterGradient(x, p, &grad[0]);
      return grad[ipar];
   }

   double fParameters[5] = {0,0,0,0,0};
   double fIntegral = 1.0; 
};

struct LikelihoodFitType {};
struct Chi2FitType {};

template <typename U, typename V, typename F>
struct GradientFittingTestTraits {
   using DataType = U;
   using FittingDataType = V;
   using FitType = F; 
};

// Typedefs of GradientTestTraits for scalar (binned and unbinned) data
using ScalarChi2 = GradientFittingTestTraits<Double_t, ROOT::Fit::BinData, Chi2FitType>;
using ScalarBinned = GradientFittingTestTraits<Double_t, ROOT::Fit::BinData, LikelihoodFitType>;
using ScalarUnBinned = GradientFittingTestTraits<Double_t, ROOT::Fit::UnBinData, LikelihoodFitType>;

// Typedefs of GradientTestTraits for vectorial (binned and unbinned) data
#ifdef R__HAS_VECCORE
using VectorialChi2 = GradientFittingTestTraits<ROOT::Double_v, ROOT::Fit::BinData, Chi2FitType>;
using VectorialBinned = GradientFittingTestTraits<ROOT::Double_v, ROOT::Fit::BinData, LikelihoodFitType>;
using VectorialUnBinned = GradientFittingTestTraits<ROOT::Double_v, ROOT::Fit::UnBinData, LikelihoodFitType>;
#endif

template <class T>
class GradientFittingTest : public ::testing::Test {
protected:
   virtual void SetUp()
   {
      // Create TF2 from model function and initialize the fit function
      std::stringstream streamTF2;
      streamTF2 << "f" << this;
      std::string nameTF2 = streamTF2.str();

      GradFunc2D<typename T::DataType> fitFunction;
      fFunction = new TF2(nameTF2.c_str(), fitFunction, 0., 1., 0, 1, 5);
      fFunction->SetNpx(300);
      fFunction->SetNpy(300);
      double p0[5] = {1., 1., 2., 3., 0.5};
      fFunction->SetParameters(p0);
      assert(fFunction->GetNpar() == 5);

      // Assure the to-be-created histogram does not replace an old one
      std::stringstream streamTH1;
      streamTH1 << "h" << this;
      std::string nameTH2 = streamTH1.str();

      auto oldTH2 = gROOT->FindObject(nameTH2.c_str());
      if (oldTH2)
         delete oldTH2;

      fHistogram = new TH2D(nameTH2.c_str(), nameTH2.c_str(), fNumPoints, 0, 1., 99, 0., 1.);

      // Fill the histogram
      gRandom->SetSeed(222);
      for (int i = 0; i < 1000000; ++i) {
         double x, y = 0;
         fFunction->GetRandom2(x, y);
         fHistogram->Fill(x, y);
      }

   
      // Create the function
      GradFunc2D<typename T::DataType> function;

      double p[5] = {50., 1., 1, 2., 1.};
      function.SetParameters(p);

      // Create the fitter from the function
      fFitter.SetFunction(function);
      //fFitter.SetFunction(function,false);
      fFitter.Config().SetMinimizer("Minuit2");
      //fFitter.Config().MinimizerOptions().SetPrintLevel(3);


      // Fill the binned or unbinned data
      FillData();

      Fit(); 
   }

   // Fill binned data
   template <class U = typename T::FittingDataType>
   typename std::enable_if<std::is_same<U, typename T::FittingDataType>::value &&
                           std::is_same<U, ROOT::Fit::BinData>::value>::type
   FillData()
   {
      // fill fit data
      fData = new ROOT::Fit::BinData(fNumPoints, 2);
      ROOT::Fit::FillData(*fData, fHistogram, fFunction);
   }

   // Fill unbinned data
   template <class U = typename T::FittingDataType>
   typename std::enable_if<std::is_same<U, typename T::FittingDataType>::value &&
                           std::is_same<U, ROOT::Fit::UnBinData>::value>::type
   FillData()
   {
      unsigned int npoints = 100*fNumPoints + 1;
      //npoints = 101;
      fData = new ROOT::Fit::UnBinData(npoints, 2);

      gRandom->SetSeed(111); // to get the same data
      for (unsigned i = 0; i < npoints; i++) {
         double xdata, ydata = 0;
         fFunction->GetRandom2(xdata, ydata);
         fData->Add(xdata, ydata);
      }

      // for unbin data we need to fix the overall normalization parameter
      fFitter.Config().ParamsSettings()[0].SetValue(1); 
      fFitter.Config().ParamsSettings()[0].Fix(); 
   }


   // Perform the Fit
   template <class F = typename T::FitType>
   typename std::enable_if<std::is_same<F, typename T::FitType>::value &&
                           std::is_same<F, LikelihoodFitType>::value>::type
   Fit()
   {
      std::cout << "Doing a likelihood Fit " << std::endl;
      fFitter.LikelihoodFit(*fData);
   }

   template <class F = typename T::FitType>
   typename std::enable_if<std::is_same<F, typename T::FitType>::value &&
                           std::is_same<F, Chi2FitType>::value>::type
   Fit()
   {
      std::cout << "Doing a chi2 Fit " << std::endl;
      fFitter.Fit(*fData);
   }
   
   TF2 *fFunction;
   typename T::FittingDataType *fData;
   TH2D *fHistogram;
   ROOT::Fit::Fitter fFitter;

   static const unsigned fNumPoints = 401;
};

// Types used by Google Test to instantiate the tests.
#ifdef R__HAS_VECCORE
typedef ::testing::Types<ScalarChi2, ScalarBinned, ScalarUnBinned, VectorialChi2, VectorialBinned, VectorialUnBinned> TestTypes;

//typedef ::testing::Types<ScalarBinned,VectorialBinned> TestTypes;
#else
typedef ::testing::Types<ScalarChi2, ScalarBinned, ScalarUnBinned> TestTypes;
#endif

// Declare that the GradientFittingTest class should be instantiated with the types defined by TestTypes
TYPED_TEST_CASE(GradientFittingTest, TestTypes);

// Test the fitting using the gradient is successful
TYPED_TEST(GradientFittingTest, GradientFitting)
{
   // TestFixture::fFitter.Config().MinimizerOptions().SetPrintLevel(3);
   EXPECT_TRUE(TestFixture::fFitter.Result().IsValid() && TestFixture::fFitter.Result().Edm() < 0.001);
   TestFixture::fFitter.Result().Print(std::cout);
}

