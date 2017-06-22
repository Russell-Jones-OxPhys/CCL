#include "ccl.h"
#include "ctest.h"
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <time.h>
#include <string.h>

#define CORR_TOLERANCE 1E-3
//Notice the actual requirement is on theta/0.1*CORR_TOLERANCE
#define CORR_FRACTION 1E-3

CTEST_DATA(corrs) {
  double Omega_c;
  double Omega_b;
  double h;
  double n_s;
  double sigma_8;
};

CTEST_SETUP(corrs) {
  data->Omega_c = 0.30;
  data->Omega_b = 0.00;
  data->h = 0.7;
  data->sigma_8=0.8;
  data->n_s = 0.96;
}

static int linecount(FILE *f)
{
  //////
  // Counts #lines from file
  int i0=0;
  char ch[1000];
  while((fgets(ch,sizeof(ch),f))!=NULL) {
    i0++;
  }
  return i0;
}

static double angular_l_inv(ccl_cosmology *cosmo,int l,CCL_ClTracer *clt1,CCL_ClTracer *clt2, int * status)
{
  if (l==0)
    return 0;
  return 1./l;//HT of this should give 1./theta
}

static double angular_l2_inv(ccl_cosmology *cosmo,int l,CCL_ClTracer *clt1,CCL_ClTracer *clt2, int * status)
{
  double l2=(double)l*(double)l;
  double z2=1.0;//z**2
  return 1./sqrt(l2+z2);//HT of this should give (exp(-k|z|)/k)
}

static double angular_l2_exp(ccl_cosmology *cosmo,int l,CCL_ClTracer *clt1,CCL_ClTracer *clt2, int * status)
{
  double l2=(double)l*(double)l;
  double a2=1;//a**2
  return exp(-0.5*l2*a2);//HT of this should give 1./(a**2)exp(-0.5*theta**2/(a**2))
}

static void compare_corr(char *compare_type,struct corrs_data * data)
{
  int status=0;
  ccl_configuration config = default_config;
  config.transfer_function_method = ccl_bbks;
  ccl_parameters params = ccl_parameters_create_flat_lcdm(data->Omega_c,data->Omega_b,data->h,data->sigma_8,data->n_s,&status);
  ccl_cosmology * cosmo = ccl_cosmology_create(params, config);
  ASSERT_NOT_NULL(cosmo);

  int nz;
  double *zarr_1,*pzarr_1,*zarr_2,*pzarr_2,*bzarr;
  if(!strcmp(compare_type,"analytic")) {
    //Create arrays for N(z)
    double zmean_1=1.0,sigz_1=0.15;
    double zmean_2=1.5,sigz_2=0.15;
    nz=512;
    zarr_1=malloc(nz*sizeof(double));
    pzarr_1=malloc(nz*sizeof(double));
    zarr_2=malloc(nz*sizeof(double));
    pzarr_2=malloc(nz*sizeof(double));
    bzarr=malloc(nz*sizeof(double));
    for(int ii=0;ii<nz;ii++) {
      double z1=zmean_1-5*sigz_1+10*sigz_1*(ii+0.5)/nz;
      double z2=zmean_2-5*sigz_2+10*sigz_2*(ii+0.5)/nz;
      double pz1=exp(-0.5*((z1-zmean_1)*(z1-zmean_1)/(sigz_1*sigz_1)));
      double pz2=exp(-0.5*((z2-zmean_2)*(z2-zmean_2)/(sigz_2*sigz_2)));
      zarr_1[ii]=z1;
      zarr_2[ii]=z2;
      pzarr_1[ii]=pz1;
      pzarr_2[ii]=pz2;
      bzarr[ii]=1.;
    }
  }
  else {
    char str[1024];
    FILE *fnz1=fopen("./tests/benchmark/codecomp_step2_outputs/bin1_histo.txt","r");
    ASSERT_NOT_NULL(fnz1);
    FILE *fnz2=fopen("./tests/benchmark/codecomp_step2_outputs/bin2_histo.txt","r");
    ASSERT_NOT_NULL(fnz2);
    nz=linecount(fnz1)-1; rewind(fnz1);
    zarr_1=malloc(nz*sizeof(double));
    pzarr_1=malloc(nz*sizeof(double));
    zarr_2=malloc(nz*sizeof(double));
    pzarr_2=malloc(nz*sizeof(double));
    bzarr=malloc(nz*sizeof(double));
    fgets(str,1024,fnz1);
    fgets(str,1024,fnz2);
    for(int ii=0;ii<nz;ii++) {
      double z1,z2,nz1,nz2;
      fscanf(fnz1,"%lf %lf",&z1,&nz1);
      fscanf(fnz2,"%lf %lf",&z2,&nz2);
      zarr_1[ii]=z1; zarr_2[ii]=z2;
      pzarr_1[ii]=nz1; pzarr_2[ii]=nz2;
      bzarr[ii]=1.;
    }
  }

  char fname[256];
  FILE *fi_dd_11,*fi_dd_12,*fi_dd_22;
  FILE *fi_ll_11_pp,*fi_ll_12_pp,*fi_ll_22_pp;
  FILE *fi_ll_11_mm,*fi_ll_12_mm,*fi_ll_22_mm;
  int has_rsd=0,has_magnification=0, has_intrinsic_alignment=0;
  int status2=0;
  CCL_ClTracer *tr_nc_1=ccl_cl_tracer_number_counts_simple_new(cosmo,nz,zarr_1,pzarr_1,nz,zarr_1,bzarr,&status2);
  ASSERT_NOT_NULL(tr_nc_1);
  CCL_ClTracer *tr_nc_2=ccl_cl_tracer_number_counts_simple_new(cosmo,nz,zarr_2,pzarr_2,nz,zarr_2,bzarr,&status2);
  ASSERT_NOT_NULL(tr_nc_2);
  CCL_ClTracer *tr_wl_1=ccl_cl_tracer_lensing_simple_new(cosmo,nz,zarr_1,pzarr_1,&status2);
  ASSERT_NOT_NULL(tr_wl_1);
  CCL_ClTracer *tr_wl_2=ccl_cl_tracer_lensing_simple_new(cosmo,nz,zarr_2,pzarr_2,&status2 );
  ASSERT_NOT_NULL(tr_wl_2);

  sprintf(fname,"tests/benchmark/codecomp_step2_outputs/run_b1b1%s_log_wt_dd.txt",compare_type);
  fi_dd_11=fopen(fname,"r"); ASSERT_NOT_NULL(fi_dd_11);
  sprintf(fname,"tests/benchmark/codecomp_step2_outputs/run_b1b2%s_log_wt_dd.txt",compare_type);
  fi_dd_12=fopen(fname,"r"); ASSERT_NOT_NULL(fi_dd_12);
  sprintf(fname,"tests/benchmark/codecomp_step2_outputs/run_b2b2%s_log_wt_dd.txt",compare_type);
  fi_dd_22=fopen(fname,"r"); ASSERT_NOT_NULL(fi_dd_22);
  sprintf(fname,"tests/benchmark/codecomp_step2_outputs/run_b1b1%s_log_wt_ll_pp.txt",compare_type);
  fi_ll_11_pp=fopen(fname,"r"); ASSERT_NOT_NULL(fi_ll_11_pp);
  sprintf(fname,"tests/benchmark/codecomp_step2_outputs/run_b1b2%s_log_wt_ll_pp.txt",compare_type);
  fi_ll_12_pp=fopen(fname,"r"); ASSERT_NOT_NULL(fi_ll_12_pp);
  sprintf(fname,"tests/benchmark/codecomp_step2_outputs/run_b2b2%s_log_wt_ll_pp.txt",compare_type);
  fi_ll_22_pp=fopen(fname,"r"); ASSERT_NOT_NULL(fi_ll_22_pp);
  sprintf(fname,"tests/benchmark/codecomp_step2_outputs/run_b1b1%s_log_wt_ll_mm.txt",compare_type);
  fi_ll_11_mm=fopen(fname,"r"); ASSERT_NOT_NULL(fi_ll_11_mm);
  sprintf(fname,"tests/benchmark/codecomp_step2_outputs/run_b1b2%s_log_wt_ll_mm.txt",compare_type);
  fi_ll_12_mm=fopen(fname,"r"); ASSERT_NOT_NULL(fi_ll_12_mm);
  sprintf(fname,"tests/benchmark/codecomp_step2_outputs/run_b2b2%s_log_wt_ll_mm.txt",compare_type);
  fi_ll_22_mm=fopen(fname,"r"); ASSERT_NOT_NULL(fi_ll_22_mm);

  double fraction_failed=0,fraction_failed_analytical=0;
  int nofl=15;
  bool taper_cl=false;
  double taper_cl_limits[4]={1,2,10000,15000};//{0,0,0,0};
  double wt_dd_11[nofl],wt_dd_12[nofl],wt_dd_22[nofl];
  double wt_dd_11_taper[nofl];
  double wt_ll_11_mm[nofl],wt_ll_12_mm[nofl],wt_ll_22_mm[nofl];
  double wt_ll_11_pp[nofl],wt_ll_12_pp[nofl],wt_ll_22_pp[nofl];
  double *analytical_l_inv,*analytical_l2_inv,*analytical_l2_exp;
  double *wt_dd_11_h,*wt_dd_12_h,*wt_dd_22_h,*wt_dd_11_h_taper;
  double *wt_ll_11_h_mm,*wt_ll_12_h_mm,*wt_ll_22_h_mm;
  double *wt_ll_11_h_pp,*wt_ll_12_h_pp,*wt_ll_22_h_pp;
  double theta_in[nofl],*theta_arr,*theta_arr_an;


  for(int ii=0;ii<nofl;ii++) {
    fscanf(fi_dd_11,"%lf %lf",&theta_in[ii],&wt_dd_11[ii]);
    fscanf(fi_dd_12,"%*lf %lf",&wt_dd_12[ii]);
    fscanf(fi_dd_22,"%*lf %lf",&wt_dd_22[ii]);
    fscanf(fi_ll_11_pp,"%*lf %lf",&wt_ll_11_pp[ii]);
    fscanf(fi_ll_12_pp,"%*lf %lf",&wt_ll_12_pp[ii]);
    fscanf(fi_ll_22_pp,"%*lf %lf",&wt_ll_22_pp[ii]);
    fscanf(fi_ll_11_mm,"%*lf %lf",&wt_ll_11_mm[ii]);
    fscanf(fi_ll_12_mm,"%*lf %lf",&wt_ll_12_mm[ii]);
    fscanf(fi_ll_22_mm,"%*lf %lf",&wt_ll_22_mm[ii]);
  }
  time_t start_time,end_time;
  double time_sec=0;

  time(&start_time);
  taper_cl=true;
  //computing on analytical functions
  ccl_tracer_corr_fftlog(cosmo,NL,&theta_arr_an,tr_nc_1,tr_nc_1,0,taper_cl,taper_cl_limits,
		   &analytical_l_inv,angular_l_inv);
  ccl_tracer_corr_fftlog(cosmo,NL,&theta_arr_an,tr_nc_1,tr_nc_1,0,taper_cl,taper_cl_limits,
		   &analytical_l2_inv,angular_l2_inv);
  ccl_tracer_corr_fftlog(cosmo,NL,&theta_arr_an,tr_nc_1,tr_nc_1,0,taper_cl,taper_cl_limits,
		   &analytical_l2_exp,angular_l2_exp);

  time(&end_time);
  time_sec=difftime(end_time,start_time);
  printf("CCL correlation Analytical done. More in progress... %.10e \n",time_sec);

  time(&start_time);
  taper_cl=true;
  //taper_cl_limits={1,2,30000,50000};
  ccl_tracer_corr(cosmo,NL,&theta_arr,tr_nc_1,tr_nc_1,0,taper_cl,taper_cl_limits,
                  &wt_dd_11_h_taper);
  taper_cl=false;
  ccl_tracer_corr(cosmo,NL,&theta_arr,tr_nc_1,tr_nc_1,0,taper_cl,taper_cl_limits,
		  &wt_dd_11_h);

  time(&end_time);
  time_sec=difftime(end_time,start_time);
  printf("CCL correlation first calculation done. More in progress... %.10e \n",time_sec);
  ccl_tracer_corr(cosmo,NL,&theta_arr,tr_nc_1,tr_nc_2,0,taper_cl,taper_cl_limits,
		  &wt_dd_12_h);
  ccl_tracer_corr(cosmo,NL,&theta_arr,tr_nc_2,tr_nc_2,0,taper_cl,taper_cl_limits,
		  &wt_dd_22_h);
  ccl_tracer_corr(cosmo,NL,&theta_arr,tr_wl_1,tr_wl_1,0,taper_cl,taper_cl_limits,
		  &wt_ll_11_h_pp);
  ccl_tracer_corr(cosmo,NL,&theta_arr,tr_wl_1,tr_wl_2,0,taper_cl,taper_cl_limits,
		  &wt_ll_12_h_pp);
  ccl_tracer_corr(cosmo,NL,&theta_arr,tr_wl_2,tr_wl_2,0,taper_cl,taper_cl_limits,
		  &wt_ll_22_h_pp);
  ccl_tracer_corr(cosmo,NL,&theta_arr,tr_wl_1,tr_wl_1,4,taper_cl,taper_cl_limits,
		  &wt_ll_11_h_mm);
  ccl_tracer_corr(cosmo,NL,&theta_arr,tr_wl_1,tr_wl_2,4,taper_cl,taper_cl_limits,
		  &wt_ll_12_h_mm);
  ccl_tracer_corr(cosmo,NL,&theta_arr,tr_wl_2,tr_wl_2,4,taper_cl,taper_cl_limits,
		  &wt_ll_22_h_mm);
  time(&end_time);
  time_sec=difftime(end_time,start_time);
  printf("CCL correlation all calculation done. %.10e \n",time_sec);
  //Re-scale theta from radians to degrees
  for (int i=0;i<NL;i++){
    theta_arr_an[i]=theta_arr_an[i]*180/M_PI;
    theta_arr[i]=theta_arr[i]*180/M_PI;
  }

  
  FILE *output2 = fopen("cc_test_corr_out_fftlog.dat", "w");
  FILE *output_analytical = fopen("cc_test_corr_out_analytical_fftlog.dat", "w");
  for (int ii=0;ii<NL;ii++){
    fprintf(output2,"%.10e %.10e %.10e %.10e %.10e %.10e %.10e %.10e %.10e %.10e %.10e \n",
	    theta_arr[ii],wt_dd_11_h[ii],wt_dd_11_h_taper[ii],wt_dd_12_h[ii],wt_dd_22_h[ii],
	    wt_ll_11_h_pp[ii],wt_ll_12_h_pp[ii],wt_ll_22_h_pp[ii],wt_ll_11_h_mm[ii],
	    wt_ll_12_h_mm[ii],wt_ll_22_h_mm[ii]);

    fprintf(output_analytical,"%.10e %.10e %.10e %.10e\n",theta_arr_an[ii],
	    analytical_l_inv[ii],analytical_l2_inv[ii],analytical_l2_exp[ii]);
  }
  fclose(output2);
  fclose(output_analytical);
  printf("CCL correlation output done. Comparison in progress...\n");

  //Spline
  gsl_spline * spl_wt_dd_11_h = gsl_spline_alloc(L_SPLINE_TYPE,NL);
  status = gsl_spline_init(spl_wt_dd_11_h, theta_arr, wt_dd_11_h, NL);
  gsl_spline * spl_wt_dd_12_h = gsl_spline_alloc(L_SPLINE_TYPE,NL);
  status = gsl_spline_init(spl_wt_dd_12_h, theta_arr, wt_dd_12_h, NL);
  gsl_spline * spl_wt_dd_22_h = gsl_spline_alloc(L_SPLINE_TYPE,NL);
  status = gsl_spline_init(spl_wt_dd_22_h, theta_arr, wt_dd_22_h, NL);
  gsl_spline * spl_wt_ll_11_h_pp = gsl_spline_alloc(L_SPLINE_TYPE,NL);
  status = gsl_spline_init(spl_wt_ll_11_h_pp, theta_arr, wt_ll_11_h_pp, NL);
  gsl_spline * spl_wt_ll_12_h_pp = gsl_spline_alloc(L_SPLINE_TYPE,NL);
  status = gsl_spline_init(spl_wt_ll_12_h_pp, theta_arr, wt_ll_12_h_pp, NL);
  gsl_spline * spl_wt_ll_22_h_pp = gsl_spline_alloc(L_SPLINE_TYPE,NL);
  status = gsl_spline_init(spl_wt_ll_22_h_pp, theta_arr, wt_ll_22_h_pp, NL);
  gsl_spline * spl_wt_ll_11_h_mm = gsl_spline_alloc(L_SPLINE_TYPE,NL);
  status = gsl_spline_init(spl_wt_ll_11_h_mm, theta_arr, wt_ll_11_h_mm, NL);
  gsl_spline * spl_wt_ll_12_h_mm = gsl_spline_alloc(L_SPLINE_TYPE,NL);
  status = gsl_spline_init(spl_wt_ll_12_h_mm, theta_arr, wt_ll_12_h_mm, NL);
  gsl_spline * spl_wt_ll_22_h_mm = gsl_spline_alloc(L_SPLINE_TYPE,NL);
  status = gsl_spline_init(spl_wt_ll_22_h_mm, theta_arr, wt_ll_22_h_mm, NL);
  printf("Splines for correlation done. Spline evaluation in progress...\n");

  int ii,istart=0,iend=nofl;
  if(theta_in[0]<theta_arr[0] || theta_in[nofl-1]>theta_arr[NL-1]){
    printf("theta_in range: [%e,%e]\n",theta_in[0],theta_in[nofl-1]);
    printf("theta_arr range: [%e,%e]\n",theta_arr[0],theta_arr[NL-1]);
    printf("This code would crash because gsl will attempt to extrapolate.\n");
    printf("Temporary solution: reducing the range for comparison to avoid extralpolation.\n");
    ii=0;
    while(theta_in[ii]<theta_arr[NL-1]){ii++;}
    iend=ii-1;
    ii=nofl-1;
    while(theta_in[ii]>theta_arr[0]){ii=ii-1;}
    istart=ii+1;
    printf("Corrected theta_in range: [%e,%e]\n",theta_in[istart],theta_in[iend]);
    printf("This correction avoids crash, but does not\n compare correlation in the full range of angles needed.\n");
  }
  
  double tmp;
  FILE *output = fopen("cc_test_corr_out.dat", "w");
  for(ii=istart;ii<iend;ii++) {

    //by-pass small thetas, we don't have requirements on those.
    //if (theta_in[ii]<0.1)
    //  continue;

    if (fabs(analytical_l_inv[ii]*2.0*M_PI*(theta_arr[ii]*M_PI/180.)-1)>CORR_TOLERANCE*theta_arr[ii]/0.1)
      fraction_failed_analytical++;

    tmp=gsl_spline_eval(spl_wt_dd_11_h, theta_in[ii], NULL);
    if(fabs(tmp/wt_dd_11[ii]-1)>CORR_TOLERANCE*theta_in[ii]/0.1)
      fraction_failed++;
    //columns 1,2,3
    fprintf(output,"%.10e %.10e %.10e",theta_in[ii],tmp,wt_dd_11[ii]);

    tmp=gsl_spline_eval(spl_wt_dd_12_h, theta_in[ii], NULL);
    if(fabs(tmp/wt_dd_12[ii]-1)>CORR_TOLERANCE*theta_in[ii]/0.1)
      fraction_failed++;
    //columns 4,5
    fprintf(output," %.10e %.10e",tmp,wt_dd_12[ii]);

    tmp=gsl_spline_eval(spl_wt_dd_22_h, theta_in[ii], NULL);
    if(fabs(tmp/wt_dd_22[ii]-1)>CORR_TOLERANCE*theta_in[ii]/0.1)
      fraction_failed++;
    //columns 6,7
    fprintf(output," %.10e %.10e",tmp,wt_dd_22[ii]);

    gsl_spline_eval_e(spl_wt_ll_11_h_pp, theta_in[ii], NULL,&tmp);
    if(fabs(tmp/wt_ll_11_pp[ii]-1)>CORR_TOLERANCE*theta_in[ii]/0.1)
      fraction_failed++;
    //columns 8,9
    fprintf(output," %.10e %.10e",tmp,wt_ll_11_pp[ii]);
    
    gsl_spline_eval_e(spl_wt_ll_12_h_pp, theta_in[ii], NULL,&tmp);
    if(fabs(tmp/wt_ll_12_pp[ii]-1)>CORR_TOLERANCE*theta_in[ii]/0.1)
      fraction_failed++;
    //columns 10,11
    fprintf(output," %.10e %.10e",tmp,wt_ll_12_pp[ii]);

    gsl_spline_eval_e(spl_wt_ll_22_h_pp, theta_in[ii], NULL,&tmp);
    if(fabs(tmp/wt_ll_22_pp[ii]-1)>CORR_TOLERANCE*theta_in[ii]/0.1)
      fraction_failed++;
    //columns 12,13
    fprintf(output," %.10e %.10e",tmp,wt_ll_22_pp[ii]);

    gsl_spline_eval_e(spl_wt_ll_11_h_mm, theta_in[ii], NULL,&tmp);
    if(fabs(tmp/wt_ll_11_mm[ii]-1)>CORR_TOLERANCE*theta_in[ii]/0.1)
      fraction_failed++;
    //columns 14,15
    fprintf(output," %.10e %.10e",tmp,wt_ll_11_mm[ii]);

    gsl_spline_eval_e(spl_wt_ll_12_h_mm, theta_in[ii], NULL,&tmp);
    if(fabs(tmp/wt_ll_12_mm[ii]-1)>CORR_TOLERANCE*theta_in[ii]/0.1)
      fraction_failed++;
    //columns 16,17
    fprintf(output," %.10e %.10e",tmp,wt_ll_12_mm[ii]);

    gsl_spline_eval_e(spl_wt_ll_22_h_mm, theta_in[ii], NULL,&tmp);
    if(fabs(tmp/wt_ll_22_mm[ii]-1)>CORR_TOLERANCE*theta_in[ii]/0.1)
      fraction_failed++;
    //columns 18,19
    fprintf(output," %.10e %.10e \n",tmp,wt_ll_22_mm[ii]);
  }
  fclose(output);
  gsl_spline_free(spl_wt_dd_11_h);
  gsl_spline_free(spl_wt_dd_12_h);
  gsl_spline_free(spl_wt_dd_22_h);
  gsl_spline_free(spl_wt_ll_11_h_pp);
  gsl_spline_free(spl_wt_ll_12_h_pp);
  gsl_spline_free(spl_wt_ll_22_h_pp);
  gsl_spline_free(spl_wt_ll_11_h_mm);
  gsl_spline_free(spl_wt_ll_12_h_mm);
  gsl_spline_free(spl_wt_ll_22_h_mm);

  fclose(fi_dd_11);
  fclose(fi_dd_12);
  fclose(fi_dd_22);
  fclose(fi_ll_11_pp);
  fclose(fi_ll_12_pp);
  fclose(fi_ll_22_pp);
  fclose(fi_ll_11_mm);
  fclose(fi_ll_12_mm);
  fclose(fi_ll_22_mm);

  fraction_failed/=9*nofl;
  printf("%lf %%\n",fraction_failed*100);
  printf("Analytical %lf %%\n",fraction_failed_analytical/nofl/1*100);
  ASSERT_TRUE((fraction_failed<CORR_FRACTION));

  free(zarr_1);
  free(zarr_2);
  free(pzarr_1);
  free(pzarr_2);
  free(bzarr);
  ccl_cosmology_free(cosmo);
}


CTEST2(corrs,histo) {
  compare_corr("histo",data);
}

CTEST2(corrs,analytic) {
  compare_corr("analytic",data);
}