#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include "ccl.h"
#include "ccl_lsst_specs.h"
#include "ccl_halomod.h"

//Test with a 'boring' cosmological model/
#define OC 0.25
#define OB 0.05
#define OK 0.00
#define ON 0.00
#define HH 0.70
#define W0 -1.0
#define WA 0.00
#define NS 0.96
#define NORMPS 0.80
#define T_CMB 2.725
#define ZD 0.0
#define NZ 128
#define Z0_GC 0.50
#define SZ_GC 0.05
#define Z0_SH 0.65
#define SZ_SH 0.05
#define NL 512
#define PS 0.1
#define NREL 3.046
#define NMAS 0
#define MNU 0.0

int main(void){
  
  int status = 0; // status flag

  double a = 1./(1.+ZD); // scale factor

  FILE *fp; // File pointer

  int test_densities = 1;
  int test_distance = 1;
  //int test_basics = 1;
  int test_mass_function = 1;
  int test_halo_properties = 1;
  //int test_nfw_wk = 1;
  int test_power = 1;

  // Halo overdensity
  double Delta_v = 200.;
  
  // Initial white space
  printf("\n"); 

  // Initialize cosmological parameters
  ccl_configuration config=default_config;

  // Set where the power spectrum comes from
  //config.transfer_function_method=ccl_boltzmann_class; //Set the linear spectrum to come from CLASS
  config.transfer_function_method=ccl_eisenstein_hu; //Set the linear spectrum to come from Eisenstein & Hu (1998) approximation

  // Set where the mass function comes from
  config.mass_function_method=ccl_shethtormen; //Set the mass function to be Sheth & Tormen (1999)
  
  ccl_parameters params = ccl_parameters_create(OC, OB, OK, NREL, NMAS, MNU, W0, WA, HH, NORMPS, NS,-1,-1,-1,-1,NULL,NULL, &status);
  
  // Initialize the cosmology object given the cosmological parameters
  ccl_cosmology *cosmo=ccl_cosmology_create(params,config);

  //
  //Now to the tests
  //

  //Test the cosmological densities
  if(test_densities==1){

    printf("Testing density\n");
    printf("\n");

    // Compute radial distances (see include/ccl_background.h for more routines)
    printf("Critical density [Msun/h / (Mpc/h)^3]: %14.7e\n", RHO_CRITICAL);
    printf("\n");
    
  }
  
  //Test the distance calculation
  if(test_distance==1){

    printf("Testing distance calculation\n");
    printf("\n");
    
    // Compute radial distances (see include/ccl_background.h for more routines)
    printf("Comoving distance to z = %.3lf is chi = %.3lf Mpc\n",
	   ZD,ccl_comoving_radial_distance(cosmo, a, &status));
    printf("\n");
  
  }

  /*
  //Test the basic halo-model numbers
  if(test_basics==1){

    printf("Testing halo-model basics\n");
    printf("\n");

    double dc=delta_c();
    double Dv=Delta_v();
    printf("delta_c: %f\n", dc);
    printf("Delta_v: %f\n", Dv);
    printf("\n");
    
  }
  */

  // Test mass function
  if(test_mass_function==1){

    // Set the mass range and number of masses to do
    double m_min=1e10;
    double m_max=1e16;
    int nm=101;

    printf("Testing mass function\n");
    printf("\n");
  
    printf("M / Msun\t nu\t\t n(M)\t\n");
    printf("=========================================\n");
    for (int i = 1; i <= nm; i++){
      double m = exp(log(m_min)+log(m_max/m_min)*((i-1.)/(nm-1.)));
      double n = ccl_nu(cosmo, m, a, &status);
      double f = ccl_massfunc(cosmo, m, a, Delta_v, &status);
      printf("%e\t %f\t %f\n", m, n, f);
    }
    printf("=========================================\n");
    printf("\n");
  
  }

  //Test halo properties
  if(test_halo_properties==1){

    // Set the mass range and number of masses to do
    double m_min=1e10;
    double m_max=1e16;
    int nm=101;

    printf("Testing halo properties\n");
    printf("\n");

    printf("M / Msun\t nu\t\t r_vir / Mpc\t r_Lag / Mpc\t conc\t\n");
    printf("==========================================================================\n");
    for (int i = 1; i <= nm; i++){
      double m = exp(log(m_min)+log(m_max/m_min)*((i-1.)/(nm-1.)));
      double n = ccl_nu(cosmo, m, a, &status); 
      double r_vir = ccl_r_delta(cosmo, m, a, Delta_v, &status);
      double r_lag = ccl_r_Lagrangian(cosmo, m, a, &status);
      double conc = ccl_halo_concentration(cosmo, m, a, &status);
      printf("%e\t %f\t %f\t %f\t %f\n", m, n, r_vir, r_lag, conc);
    }
    printf("==========================================================================\n");
    printf("\n");
    
  }
  
  /*
  //Test the halo Fourier Transform
  if(test_nfw_wk==1){

    // k range and number of points in k
    double kmin = 1e-3;
    double kmax = 1e2;
    int nk = 101;

    // Halo mass in Msun
    double m = 1e15; 

    printf("Testing halo Fourier Transform\n");
    printf("\n");
    printf("Halo mass [Msun]: %e\n", m);
    double c=ccl_halo_concentration(cosmo, m, a, &status);
    printf("Halo concentration: %f\n", c);
    printf("\n");

    fp = fopen("Mead/CCL_Wk.dat", "w");

    printf("k / Mpc^-1\t Wk\t\n");
    printf("=============================\n");
    for (int i = 1; i <= nk; i++){

      double k = exp(log(kmin)+log(kmax/kmin)*(i-1.)/(nk-1.));
      double wk = u_nfw_c(cosmo, c, m, k, a, &status);

      printf("%e\t %e\n", k, wk);
      fprintf(fp, "%e\t %e\n", k, wk);
      
    }
    printf("=============================\n");

    fclose(fp);
    printf("\n");
    
    
  }
  */

  // Test the power spectrum calculation
  if(test_power==1){

    // Set range (note that the round numbers are in k/h)
    double kmin=1e-3*cosmo->params.h;
    double kmax=1e2*cosmo->params.h;
    int nk=200;

    printf("Testing power spectrum calculation\n");
    printf("\n");

    for (int iz = 1; iz <= 2; iz++){

      // z=0
      if(iz==1){
	a=1.0;
	fp = fopen("/Users/Mead/Physics/CCL_halomod_tests/data/CCL_power_z0.dat", "w");
      } 

      // z=1
      if(iz==2){
	a=0.5;
	fp = fopen("/Users/Mead/Physics/CCL_halomod_tests/data/CCL_power_z1.dat", "w");
      } 
  
      printf("k\t\t P_lin\t\t P_NL\t\t P_2h\t\t P_1h\t\t P_halo\t\n");
      printf("=============================================================================================\n");    
      for (int i = 1; i <= nk; i++){

	double k = exp(log(kmin)+log(kmax/kmin)*(i-1.)/(nk-1.));
    
	double p_lin = ccl_linear_matter_power(cosmo, k, a, &status); // Linear spectrum
	double p_nl = ccl_nonlin_matter_power(cosmo, k, a, &status); // Non-linear spectrum
	double p_twohalo = ccl_p_2h(cosmo, k, a, &status); // Two-halo power
	double p_onehalo = ccl_p_1h(cosmo, k, a, &status); // One-halo power      
	double p_full = ccl_p_halomod(cosmo, k, a, &status); // Full halo-model power

	printf("%e\t %e\t %e\t %e\t %e\t %e\n", k, p_lin, p_nl, p_twohalo, p_onehalo, p_full);
	fprintf(fp, "%e\t %e\t %e\t %e\t %e\t %e\n", k, p_lin, p_nl, p_twohalo, p_onehalo, p_full);

      }
      printf("=============================================================================================\n");
      printf("\n");
      fclose(fp);
  
    }

  }
  
  // Always clean up the cosmology object!!
  ccl_cosmology_free(cosmo);

  // 0 is a successful return
  return 0;
}