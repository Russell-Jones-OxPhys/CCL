#include "ccl_cls.h"
#include "ccl_power.h"
#include "ccl_background.h"
#include "ccl_error.h"
#include "ccl_utils.h"
#include <stdlib.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "gsl/gsl_errno.h"
#include "gsl/gsl_integration.h"
#include "ccl_params.h"

//Spline creator
//n     -> number of points
//x     -> x-axis
//y     -> f(x)-axis
//y0,yf -> values of f(x) to use beyond the interpolation range
static SplPar *spline_init(int n,double *x,double *y,double y0,double yf)
{
  SplPar *spl=malloc(sizeof(SplPar));
  if(spl==NULL)
    return NULL;
  
  spl->intacc=gsl_interp_accel_alloc();
  spl->spline=gsl_spline_alloc(gsl_interp_cspline,n);
  int parstatus=gsl_spline_init(spl->spline,x,y,n);
  if(parstatus) {
    gsl_interp_accel_free(spl->intacc);
    gsl_spline_free(spl->spline);
    return NULL;
  }

  spl->x0=x[0];
  spl->xf=x[n-1];
  spl->y0=y0;
  spl->yf=yf;

  return spl;
}

//Evaluates spline at x checking for bound errors
static double spline_eval(double x,SplPar *spl)
{
  if(x<=spl->x0)
    return spl->y0;
  else if(x>=spl->xf) 
    return spl->yf;
  else
    return gsl_spline_eval(spl->spline,x,spl->intacc);
}

//Wrapper around spline_eval with GSL function syntax
static double speval_bis(double x,void *params)
{
  return spline_eval(x,(SplPar *)params);
}

//Spline destructor
static void spline_free(SplPar *spl)
{
  gsl_spline_free(spl->spline);
  gsl_interp_accel_free(spl->intacc);
  free(spl);
}

//Params for lensing kernel integrand
typedef struct {
  double chi;
  SplPar *spl_pz;
  ccl_cosmology *cosmo;
  int *status;
} IntLensPar;

//Integrand for lensing kernel
static double integrand_wl(double chip,void *params)
{
  IntLensPar *p=(IntLensPar *)params;
  double chi=p->chi;
  double a=ccl_scale_factor_of_chi(p->cosmo,chip, p->status);
  double z=1./a-1;
  double pz=spline_eval(z,p->spl_pz);
  double h=p->cosmo->params.h*ccl_h_over_h0(p->cosmo,a, p->status)/CLIGHT_HMPC;

  if(chi==0)
    return h*pz;
  else
    return h*pz*ccl_sinn(p->cosmo,chip-chi,p->status)/ccl_sinn(p->cosmo,chip,p->status);
}

//Integral to compute lensing window function
//chi     -> comoving distance
//cosmo   -> ccl_cosmology object
//spl_pz  -> normalized N(z) spline
//chi_max -> maximum comoving distance to which the integral is computed
//win     -> result is stored here
static int window_lensing(double chi,ccl_cosmology *cosmo,SplPar *spl_pz,double chi_max,double *win)
{
  int gslstatus =0, status =0;
  double result,eresult;
  IntLensPar ip;
  gsl_function F;
  gsl_integration_workspace *w=gsl_integration_workspace_alloc(1000);

  ip.chi=chi;
  ip.cosmo=cosmo;
  ip.spl_pz=spl_pz;
  ip.status = &status;
  F.function=&integrand_wl;
  F.params=&ip;
  gslstatus=gsl_integration_qag(&F,chi,chi_max,0,1E-4,1000,GSL_INTEG_GAUSS41,w,&result,&eresult);
  *win=result;
  gsl_integration_workspace_free(w);
  if(gslstatus!=GSL_SUCCESS || *ip.status)
    return 1;
  //TODO: chi_max should be changed to chi_horizon
  //we should precompute this quantity and store it in cosmo by default

  return 0;
}

//Params for lensing kernel integrand
typedef struct {
  double chi;
  SplPar *spl_pz;
  SplPar *spl_sz;
  ccl_cosmology *cosmo;
  int *status;
} IntMagPar;

//Integrand for magnification kernel
static double integrand_mag(double chip,void *params)
{
  IntMagPar *p=(IntMagPar *)params;
//EK: added local status here as the status testing is done in routines called from this function
  int status;
  double chi=p->chi;
  double a=ccl_scale_factor_of_chi(p->cosmo,chip, p->status);
  double z=1./a-1;
  double pz=spline_eval(z,p->spl_pz);
  double sz=spline_eval(z,p->spl_sz);
  double h=p->cosmo->params.h*ccl_h_over_h0(p->cosmo,a, p->status)/CLIGHT_HMPC;

  if(chi==0)
    return h*pz*(1-2.5*sz);
  else
    return h*pz*(1-2.5*sz)*ccl_sinn(p->cosmo,chip-chi,p->status)/ccl_sinn(p->cosmo,chip,p->status);
}

//Integral to compute magnification window function
//chi     -> comoving distance
//cosmo   -> ccl_cosmology object
//spl_pz  -> normalized N(z) spline
//spl_pz  -> magnification bias s(z)
//chi_max -> maximum comoving distance to which the integral is computed
//win     -> result is stored here
static int window_magnification(double chi,ccl_cosmology *cosmo,SplPar *spl_pz,SplPar *spl_sz,
				double chi_max,double *win)
{
  int gslstatus =0, status =0;
  double result,eresult;
  IntMagPar ip;
  gsl_function F;
  gsl_integration_workspace *w=gsl_integration_workspace_alloc(1000);

  ip.chi=chi;
  ip.cosmo=cosmo;
  ip.spl_pz=spl_pz;
  ip.spl_sz=spl_sz;
  ip.status = &status;
  F.function=&integrand_mag;
  F.params=&ip;
  gslstatus=gsl_integration_qag(&F,chi,chi_max,0,1E-4,1000,GSL_INTEG_GAUSS41,w,&result,&eresult);
  *win=result;
  gsl_integration_workspace_free(w);
  if(gslstatus!=GSL_SUCCESS || *ip.status)
    return 1;
  //TODO: chi_max should be changed to chi_horizon
  //we should precompute this quantity and store it in cosmo by default

  return 0;
}

//CCL_ClTracer creator
//cosmo   -> ccl_cosmology object
//tracer_type -> type of tracer. Supported: CL_TRACER_NC, CL_TRACER_WL
//nz_n -> number of points for N(z)
//z_n  -> array of z-values for N(z)
//n    -> corresponding N(z)-values. Normalization is irrelevant
//        N(z) will be set to zero outside the range covered by z_n
//nz_b -> number of points for b(z)
//z_b  -> array of z-values for b(z)
//b    -> corresponding b(z)-values.
//        b(z) will be assumed constant outside the range covered by z_n
static CCL_ClTracer *cl_tracer_new(ccl_cosmology *cosmo,int tracer_type,
				   int has_rsd,int has_magnification,int has_intrinsic_alignment,
				   int nz_n,double *z_n,double *n,
				   int nz_b,double *z_b,double *b,
				   int nz_s,double *z_s,double *s,
				   int nz_ba,double *z_ba,double *ba,
				   int nz_rf,double *z_rf,double *rf, int * status)
{
  int clstatus=0;
  CCL_ClTracer *clt=malloc(sizeof(CCL_ClTracer));
  if(clt==NULL) {
    *status=CCL_ERROR_MEMORY;
    strcpy(cosmo->status_message,"ccl_cls.c: ccl_cl_tracer_new(): memory allocation\n");
    return NULL;
  }
  clt->tracer_type=tracer_type;

  double hub=cosmo->params.h*ccl_h_over_h0(cosmo,1.,status)/CLIGHT_HMPC;
  clt->prefac_lensing=1.5*hub*hub*cosmo->params.Omega_m;

  if((tracer_type==CL_TRACER_NC)||(tracer_type==CL_TRACER_WL)) {
    clt->chimax=ccl_comoving_radial_distance(cosmo,1./(1+z_n[nz_n-1]), status);
    clt->spl_nz=spline_init(nz_n,z_n,n,0,0);
    if(clt->spl_nz==NULL) {
      free(clt);
      *status=CCL_ERROR_SPLINE;
      strcpy(cosmo->status_message,"ccl_cls.c: ccl_cl_tracer_new(): error initializing spline for N(z)\n");
      return NULL;
    }

    //Normalize n(z)
    gsl_function F;
    double nz_norm,nz_enorm;
    double *nz_normalized=malloc(nz_n*sizeof(double));
    if(nz_normalized==NULL) {
      spline_free(clt->spl_nz);
      free(clt);
      *status=CCL_ERROR_MEMORY;
      strcpy(cosmo->status_message,"ccl_cls.c: ccl_cl_tracer_new(): memory allocation\n");
      return NULL;
    }

    gsl_integration_workspace *w=gsl_integration_workspace_alloc(1000);
    F.function=&speval_bis;
    F.params=clt->spl_nz;
    clstatus=gsl_integration_qag(&F,z_n[0],z_n[nz_n-1],0,1E-4,1000,GSL_INTEG_GAUSS41,w,&nz_norm,&nz_enorm);
    gsl_integration_workspace_free(w); //TODO:check for integration errors
    if(clstatus!=GSL_SUCCESS) {
      spline_free(clt->spl_nz);
      free(clt);
      *status=CCL_ERROR_INTEG;
      strcpy(cosmo->status_message,"ccl_cls.c: ccl_cl_tracer_new(): integration error when normalizing N(z)\n");
      return NULL;
    }
    for(int ii=0;ii<nz_n;ii++)
      nz_normalized[ii]=n[ii]/nz_norm;
    spline_free(clt->spl_nz);
    clt->spl_nz=spline_init(nz_n,z_n,nz_normalized,0,0);
    free(nz_normalized);
    if(clt->spl_nz==NULL) {
      free(clt);
      *status=CCL_ERROR_SPLINE;
      strcpy(cosmo->status_message,"ccl_cls.c: ccl_cl_tracer_new(): error initializing normalized spline for N(z)\n");
      return NULL;
    }

    if(tracer_type==CL_TRACER_NC) {
      //Initialize bias spline
      clt->spl_bz=spline_init(nz_b,z_b,b,b[0],b[nz_b-1]);
      if(clt->spl_bz==NULL) {
	spline_free(clt->spl_nz);
	free(clt);
	*status=CCL_ERROR_SPLINE;
	strcpy(cosmo->status_message,"ccl_cls.c: ccl_cl_tracer_new(): error initializing spline for b(z)\n");
	return NULL;
      }
      clt->has_rsd=has_rsd;
      clt->has_magnification=has_magnification;
      if(clt->has_magnification) {
	//Compute weak lensing kernel
	int nchi;
	double *x,*y;
	double dchi=5.;
	double zmax=clt->spl_nz->xf;
	double chimax=ccl_comoving_radial_distance(cosmo,1./(1+zmax),status);
	//TODO: The interval in chi (5. Mpc) should be made a macro

	clt->spl_sz=spline_init(nz_s,z_s,s,s[0],s[nz_s-1]);
	if(clt->spl_sz==NULL) {
	  spline_free(clt->spl_nz);
	  spline_free(clt->spl_bz);
	  free(clt);
	  *status=CCL_ERROR_SPLINE;
	  strcpy(cosmo->status_message,"ccl_cls.c: ccl_cl_tracer_new(): error initializing spline for s(z)\n");
	  return NULL;
	}

	clt->chimin=0;
	nchi=(int)(chimax/dchi)+1;
	x=ccl_linear_spacing(0.,chimax,nchi);
	dchi=chimax/nchi;
	if(x==NULL || (fabs(x[0]-0)>1E-5) || (fabs(x[nchi-1]-chimax)>1e-5)) {
	  spline_free(clt->spl_nz);
	  spline_free(clt->spl_bz);
	  spline_free(clt->spl_sz);
	  free(clt);
	  *status=CCL_ERROR_LINSPACE;
	  strcpy(cosmo->status_message,
		 "ccl_cls.c: ccl_cl_tracer_new(): Error creating linear spacing in chi\n");
	  return NULL;
	}
	y=malloc(nchi*sizeof(double));
	if(y==NULL) {
	  free(x);
	  spline_free(clt->spl_nz);
	  spline_free(clt->spl_bz);
	  spline_free(clt->spl_sz);
	  free(clt);
	  *status=CCL_ERROR_MEMORY;
	  strcpy(cosmo->status_message,"ccl_cls.c: ccl_cl_tracer_new(): memory allocation\n");
	  return NULL;
	}
      
	for(int j=0;j<nchi;j++)
	  clstatus|=window_magnification(x[j],cosmo,clt->spl_nz,clt->spl_sz,chimax,&(y[j]));
	if(clstatus) {
	  free(y);
	  free(x);
	  spline_free(clt->spl_nz);
	  spline_free(clt->spl_bz);
	  spline_free(clt->spl_sz);
	  free(clt);
	  *status=CCL_ERROR_INTEG;
	  strcpy(cosmo->status_message,"ccl_cls.c: ccl_cl_tracer_new(): error computing lensing window\n");
	  return NULL;
	}

	clt->spl_wM=spline_init(nchi,x,y,y[0],0);
	if(clt->spl_wM==NULL) {
	  free(y);
	  free(x);
	  spline_free(clt->spl_nz);
	  spline_free(clt->spl_bz);
	  spline_free(clt->spl_sz);
	  free(clt);
	  *status=CCL_ERROR_SPLINE;
	  strcpy(cosmo->status_message,
		 "ccl_cls.c: ccl_cl_tracer_new(): error initializing spline for lensing window\n");
	  return NULL;
	}
	free(x); free(y);
      }
      clt->chimin=ccl_comoving_radial_distance(cosmo,1./(1+z_n[0]),status);
    }
    else if(tracer_type==CL_TRACER_WL) {
      //Compute weak lensing kernel
      int nchi;
      double *x,*y;
      double dchi=5.;
      double zmax=clt->spl_nz->xf;
      double chimax=ccl_comoving_radial_distance(cosmo,1./(1+zmax),status);
      //TODO: The interval in chi (5. Mpc) should be made a macro
      clt->chimin=0;
      nchi=(int)(chimax/dchi)+1;
      x=ccl_linear_spacing(0.,chimax,nchi);
      dchi=chimax/nchi;
      if(x==NULL || (fabs(x[0]-0)>1E-5) || (fabs(x[nchi-1]-chimax)>1e-5)) {
	spline_free(clt->spl_nz);
	free(clt);
	*status=CCL_ERROR_LINSPACE;
	strcpy(cosmo->status_message,"ccl_cls.c: ccl_cl_tracer_new(): Error creating linear spacing in chi\n");
	return NULL;
      }
      y=malloc(nchi*sizeof(double));
      if(y==NULL) {
	free(x);
	spline_free(clt->spl_nz);
	free(clt);
	*status=CCL_ERROR_MEMORY;
	strcpy(cosmo->status_message,"ccl_cls.c: ccl_cl_tracer_new(): memory allocation\n");
	return NULL;
      }
      
      for(int j=0;j<nchi;j++)
	clstatus|=window_lensing(x[j],cosmo,clt->spl_nz,chimax,&(y[j]));
      if(clstatus) {
	free(y);
	free(x);
	spline_free(clt->spl_nz);
	free(clt);
	*status=CCL_ERROR_INTEG;
	strcpy(cosmo->status_message,"ccl_cls.c: ccl_cl_tracer_new(): error computing lensing window\n");
	return NULL;
      }

      clt->spl_wL=spline_init(nchi,x,y,y[0],0);
      if(clt->spl_wL==NULL) {
	free(y);
	free(x);
	spline_free(clt->spl_nz);
	free(clt);
	*status=CCL_ERROR_SPLINE;
	strcpy(cosmo->status_message,"ccl_cls.c: ccl_cl_tracer_new(): error initializing spline for lensing window\n");
	return NULL;
      }
      free(x); free(y);
      
      clt->has_intrinsic_alignment=has_intrinsic_alignment;
      if(clt->has_intrinsic_alignment) {
	clt->spl_rf=spline_init(nz_rf,z_rf,rf,rf[0],rf[nz_rf-1]);
	if(clt->spl_rf==NULL) {
	  spline_free(clt->spl_nz);
	  free(clt);
	  *status=CCL_ERROR_SPLINE;
	  strcpy(cosmo->status_message,"ccl_cls.c: ccl_cl_tracer_new(): error initializing spline for rf(z)\n");
	  return NULL;
	}
	clt->spl_ba=spline_init(nz_ba,z_ba,ba,ba[0],ba[nz_ba-1]);
	if(clt->spl_ba==NULL) {
	  spline_free(clt->spl_rf);
	  spline_free(clt->spl_nz);
	  free(clt);
	  *status=CCL_ERROR_SPLINE;
	  strcpy(cosmo->status_message,"ccl_cls.c: ccl_cl_tracer_new(): error initializing spline for ba(z)\n");
	  return NULL;
	}
      }
    }
  }
  else {
    *status=CCL_ERROR_INCONSISTENT;
    strcpy(cosmo->status_message,"ccl_cls.c: ccl_cl_tracer_new(): unknown tracer type\n");
    return NULL;
  }

  return clt;
}

//CCL_ClTracer constructor with error checking
//cosmo   -> ccl_cosmology object
//tracer_type -> type of tracer. Supported: CL_TRACER_NC, CL_TRACER_WL
//nz_n -> number of points for N(z)
//z_n  -> array of z-values for N(z)
//n    -> corresponding N(z)-values. Normalization is irrelevant
//        N(z) will be set to zero outside the range covered by z_n
//nz_b -> number of points for b(z)
//z_b  -> array of z-values for b(z)
//b    -> corresponding b(z)-values.
//        b(z) will be assumed constant outside the range covered by z_n
CCL_ClTracer *ccl_cl_tracer_new(ccl_cosmology *cosmo,int tracer_type,
				int has_rsd,int has_magnification,int has_intrinsic_alignment,
				int nz_n,double *z_n,double *n,
				int nz_b,double *z_b,double *b,
				int nz_s,double *z_s,double *s,
				int nz_ba,double *z_ba,double *ba,
				int nz_rf,double *z_rf,double *rf, int * status)
{
  CCL_ClTracer *clt=cl_tracer_new(cosmo,tracer_type,has_rsd,has_magnification,has_intrinsic_alignment,
				  nz_n,z_n,n,nz_b,z_b,b,nz_s,z_s,s,nz_ba,z_ba,ba,nz_rf,z_rf,rf, status);
  ccl_check_status(cosmo,status);
  return clt;
}

//CCL_ClTracer destructor
void ccl_cl_tracer_free(CCL_ClTracer *clt)
{
  spline_free(clt->spl_nz);
  if(clt->tracer_type==CL_TRACER_NC) {
    spline_free(clt->spl_bz);
    if(clt->has_magnification) {
      spline_free(clt->spl_sz);
      spline_free(clt->spl_wM);
    }
  }
  else if(clt->tracer_type==CL_TRACER_WL) {
    spline_free(clt->spl_wL);
    if(clt->has_intrinsic_alignment) {
      spline_free(clt->spl_ba);
      spline_free(clt->spl_rf);
    }
  }
  free(clt);
}

CCL_ClTracer *ccl_cl_tracer_number_counts_new(ccl_cosmology *cosmo,
					      int has_rsd,int has_magnification,
					      int nz_n,double *z_n,double *n,
					      int nz_b,double *z_b,double *b,
					      int nz_s,double *z_s,double *s, int * status)
{
  return ccl_cl_tracer_new(cosmo,CL_TRACER_NC,has_rsd,has_magnification,0,
			   nz_n,z_n,n,nz_b,z_b,b,nz_s,z_s,s,
			   -1,NULL,NULL,-1,NULL,NULL, status);
}

CCL_ClTracer *ccl_cl_tracer_number_counts_simple_new(ccl_cosmology *cosmo,
						     int nz_n,double *z_n,double *n,
						     int nz_b,double *z_b,double *b, int * status)
{
  return ccl_cl_tracer_new(cosmo,CL_TRACER_NC,0,0,0,
			   nz_n,z_n,n,nz_b,z_b,b,-1,NULL,NULL,
			   -1,NULL,NULL,-1,NULL,NULL, status);
}

CCL_ClTracer *ccl_cl_tracer_lensing_new(ccl_cosmology *cosmo,
					int has_alignment,
					int nz_n,double *z_n,double *n,
					int nz_ba,double *z_ba,double *ba,
					int nz_rf,double *z_rf,double *rf, int * status)
{
  return ccl_cl_tracer_new(cosmo,CL_TRACER_WL,0,0,has_alignment,
			   nz_n,z_n,n,-1,NULL,NULL,-1,NULL,NULL,
			   nz_ba,z_ba,ba,nz_rf,z_rf,rf, status);
}

CCL_ClTracer *ccl_cl_tracer_lensing_simple_new(ccl_cosmology *cosmo,
					       int nz_n,double *z_n,double *n, int * status)
{
  return ccl_cl_tracer_new(cosmo,CL_TRACER_WL,0,0,0,
			   nz_n,z_n,n,-1,NULL,NULL,-1,NULL,NULL,
			   -1,NULL,NULL,-1,NULL,NULL, status);
}

static double j_bessel_limber(int l,double k)
{
  return sqrt(M_PI/(2*l+1.))/k;
}

static double f_dens(double a,ccl_cosmology *cosmo,CCL_ClTracer *clt, int * status)
{
  double z=1./a-1;
  double pz=spline_eval(z,clt->spl_nz);
  double bz=spline_eval(z,clt->spl_bz);
  double h=cosmo->params.h*ccl_h_over_h0(cosmo,a,status)/CLIGHT_HMPC;

  return pz*bz*h;
}

static double f_rsd(double a,ccl_cosmology *cosmo,CCL_ClTracer *clt, int * status)
{
  double z=1./a-1;
  double pz=spline_eval(z,clt->spl_nz);
  double fg=ccl_growth_rate(cosmo,a,status);
  double h=cosmo->params.h*ccl_h_over_h0(cosmo,a,status)/CLIGHT_HMPC;

  return pz*fg*h;
}

static double f_mag(double a,double chi,ccl_cosmology *cosmo,CCL_ClTracer *clt, int * status)
{
  double wM=spline_eval(chi,clt->spl_wM);
  
  if(wM<=0)
    return 0;
  else
    return wM/(a*chi);
}

#define DCHI 3.
#define LLIMBER -100
  
//Transfer function for number counts
//l -> angular multipole
//k -> wavenumber modulus
//cosmo -> ccl_cosmology object
//clt -> CCL_ClTracer object (must be of the CL_TRACER_NC type)
static double transfer_nc(int l,double k,ccl_cosmology *cosmo,CCL_ClTracer *clt, int * status)
{
  double ret=0;
  if(l>LLIMBER) {
    double x0=(l+0.5);
    double chi0=x0/k;
    if(chi0<=clt->chimax) {
      double a0=ccl_scale_factor_of_chi(cosmo,chi0,status);
      double pk0=ccl_nonlin_matter_power(cosmo,k,a0,status);
      double jl0=j_bessel_limber(l,k);
      double f_all=f_dens(a0,cosmo,clt,status)*jl0;
      if(clt->has_rsd) {
	double x1=(l+1.5);
	double chi1=x1/k;
	double a1=ccl_scale_factor_of_chi(cosmo,chi1,status);
	double pk1=ccl_nonlin_matter_power(cosmo,k,a1,status);
	double fg0=f_rsd(chi0,cosmo,clt,status);
	double fg1=f_rsd(chi1,cosmo,clt,status);
	double jl1=j_bessel_limber(l+1,k);
	f_all+=fg0*(1.-l*(l-1.)/(x0*x0))*jl0-fg1*2.*jl1*sqrt(pk1/pk0)/x1;
      }
      if(clt->has_magnification)
	f_all+=-2*clt->prefac_lensing*l*(l+1)*f_mag(a0,chi0,cosmo,clt,status)*jl0/(k*k);
      ret=f_all*sqrt(pk0);
    }
  }
  else {
    int i,nchi=(int)((clt->chimax-clt->chimin)/DCHI)+1;
    for(i=0;i<nchi;i++) {
      double chi=clt->chimin+DCHI*(i+0.5);
      if(chi<=clt->chimax) {
	double a=ccl_scale_factor_of_chi(cosmo,chi,status);
	double pk=ccl_nonlin_matter_power(cosmo,k,a,status);
	double jl=ccl_j_bessel(l,k*chi);
	double f_all=f_dens(a,cosmo,clt,status)*jl;
	if(clt->has_rsd) {
	  double ddjl,x=k*chi;
	  if(x<1E-10) {
	    if(l==0) ddjl=0.3333-0.1*x*x;
	    else if(l==2) ddjl=-0.13333333333+0.05714285714285714*x*x;
	    else ddjl=0;
          }
	  else {
	    double jlp1=ccl_j_bessel(l+1,x);
	    ddjl=((x*x-l*(l-1))*jl-2*x*jlp1)/(x*x);
	  }
	  f_all+=f_rsd(a,cosmo,clt,status)*ddjl;
	}
	if(clt->has_magnification)
	  f_all+=-2*clt->prefac_lensing*l*(l+1)*f_mag(a,chi,cosmo,clt,status)*jl/(k*k);
	
	ret+=f_all*sqrt(pk); //TODO: is it worth splining this sqrt?
      }
    }
    ret*=DCHI;
  }

  return ret;
}

static double f_lensing(double a,double chi,ccl_cosmology *cosmo,CCL_ClTracer *clt, int * status)
{
  double wL=spline_eval(chi,clt->spl_wL);
  
  if(wL<=0)
    return 0;
  else
    return clt->prefac_lensing*wL/(a*chi);
}

static double f_IA_NLA(double a,double chi,ccl_cosmology *cosmo,CCL_ClTracer *clt, int * status)
{
  if(chi<=1E-10)
    return 0;
  else {
    double a=ccl_scale_factor_of_chi(cosmo,chi, status);
    double z=1./a-1;
    double pz=spline_eval(z,clt->spl_nz);
    double ba=spline_eval(z,clt->spl_ba);
    double rf=spline_eval(z,clt->spl_rf);
    double h=cosmo->params.h*ccl_h_over_h0(cosmo,a,status)/CLIGHT_HMPC;
    
    return pz*ba*rf*h/(chi*chi);
  }
}


//Transfer function for shear
//l -> angular multipole
//k -> wavenumber modulus
//cosmo -> ccl_cosmology object
//clt -> CCL_ClTracer object (must be of the CL_TRACER_WL type)
static double transfer_wl(int l,double k,ccl_cosmology *cosmo,CCL_ClTracer *clt, int * status)
{
  double ret=0;
  if(l>LLIMBER) {
    double chi=(l+0.5)/k;
    if(chi<=clt->chimax) {
      double a=ccl_scale_factor_of_chi(cosmo,chi,status);
      double pk=ccl_nonlin_matter_power(cosmo,k,a,status);
      double jl=j_bessel_limber(l,k);
      double f_all=f_lensing(a,chi,cosmo,clt,status)*jl;
      if(clt->has_intrinsic_alignment)
	f_all+=f_IA_NLA(a,chi,cosmo,clt,status)*jl;

      ret=f_all*sqrt(pk);
    }
  }
  else {
    int i,nchi=(int)((clt->chimax-clt->chimin)/DCHI)+1;
    for(i=0;i<nchi;i++) {
      double chi=clt->chimin+DCHI*(i+0.5);
      if(chi<=clt->chimax) {
	double a=ccl_scale_factor_of_chi(cosmo,chi,status);
	double pk=ccl_nonlin_matter_power(cosmo,k,a,status);
	double jl=ccl_j_bessel(l,k*chi);
	double f_all=f_lensing(a,chi,cosmo,clt,status)*jl;
	if(clt->has_intrinsic_alignment)
	  f_all+=f_IA_NLA(a,chi,cosmo,clt,status)*jl;
	
	ret+=f_all*sqrt(pk); //TODO: is it worth splining this sqrt?
      }
    }
    ret*=DCHI;
  }

  //  return sqrt((l+2.)*(l+1.)*l*(l-1.))*ret/(k*k);
  return (l+1.)*l*ret/(k*k);
}

//Wrapper for transfer function
//l -> angular multipole
//k -> wavenumber modulus
//cosmo -> ccl_cosmology object
//clt -> CCL_ClTracer object
static double transfer_wrap(int l,double k,ccl_cosmology *cosmo,CCL_ClTracer *clt, int * status)
{
  double transfer_out=0;

  if(clt->tracer_type==CL_TRACER_NC)
    transfer_out=transfer_nc(l,k,cosmo,clt, status);
  else if(clt->tracer_type==CL_TRACER_WL)
    transfer_out=transfer_wl(l,k,cosmo,clt, status);
  else
    transfer_out=-1;
  return transfer_out;
}

//Params for power spectrum integrand
typedef struct {
  int l;
  ccl_cosmology *cosmo;
  CCL_ClTracer *clt1;
  CCL_ClTracer *clt2;
  int *status;
} IntClPar;

//Integrand for integral power spectrum
static double cl_integrand(double lk,void *params)
{
  double d1,d2;
  IntClPar *p=(IntClPar *)params;
  double k=pow(10.,lk);
  d1=transfer_wrap(p->l,k,p->cosmo,p->clt1,p->status);
  d2=transfer_wrap(p->l,k,p->cosmo,p->clt2,p->status);

  return k*k*k*d1*d2;
}

//Figure out k intervals where the Limber kernel has support
//clt1 -> tracer #1
//clt2 -> tracer #2
//l    -> angular multipole
//lkmin, lkmax -> log10 of the range of scales where the transfer functions have support
static void get_k_interval(ccl_cosmology *cosmo,CCL_ClTracer *clt1,CCL_ClTracer *clt2,int l,
			   double *lkmin,double *lkmax)
{
  double chimin,chimax;
  if(clt1->tracer_type==CL_TRACER_NC) {
    if(clt2->tracer_type==CL_TRACER_NC) {
      chimin=fmax(clt1->chimin,clt2->chimin);
      chimax=fmin(clt1->chimax,clt2->chimax);
    }
    else {
      chimin=clt1->chimin;
      chimax=clt1->chimax;
    }
  }
  else if(clt2->tracer_type==CL_TRACER_NC) {
    chimin=clt2->chimin;
    chimax=clt2->chimax;
  }
  else {
    chimin=0.5*(l+0.5)/ccl_splines->K_MAX;
    chimax=2*(l+0.5)/ccl_splines->K_MIN_DEFAULT;
  }

  if(chimin<=0)
    chimin=0.5*(l+0.5)/ccl_splines->K_MAX;

  *lkmax=fmin( 2,log10(2  *(l+0.5)/chimin));
  *lkmin=fmax(-4,log10(0.5*(l+0.5)/chimax));
}

//Compute angular power spectrum between two bins
//cosmo -> ccl_cosmology object
//l -> angular multipole
//clt1 -> tracer #1
//clt2 -> tracer #2
double ccl_angular_cl(ccl_cosmology *cosmo,int l,CCL_ClTracer *clt1,CCL_ClTracer *clt2, int * status)
{
  int clastatus=0, qagstatus;
  IntClPar ipar;
  double result=0,eresult;
  double lkmin,lkmax;
  gsl_function F;
  gsl_integration_workspace *w=gsl_integration_workspace_alloc(1000);

  ipar.l=l;
  ipar.cosmo=cosmo;
  ipar.clt1=clt1;
  ipar.clt2=clt2;
  ipar.status = &clastatus;
  F.function=&cl_integrand;
  F.params=&ipar;
  get_k_interval(cosmo,clt1,clt2,l,&lkmin,&lkmax);
  qagstatus=gsl_integration_qag(&F,lkmin,lkmax,0,1E-4,1000,GSL_INTEG_GAUSS41,w,&result,&eresult);
  gsl_integration_workspace_free(w);
  if(qagstatus!=GSL_SUCCESS || *ipar.status) {
    *status=CCL_ERROR_INTEG;
    strcpy(cosmo->status_message,"ccl_cls.c: ccl_angular_cl(): error integrating over k\n");
    return -1;
  }
  ccl_check_status(cosmo,status);

  return result*M_LN10*2./M_PI;
}
