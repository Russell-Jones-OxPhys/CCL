#pragma once

double * ccl_linear_spacing(double xmin, double xmax, int N);
//Returns array of  N linearly-spaced values between xmin and xmax

double * ccl_log_spacing(double xmin, double xmax, int N);
//Returns array of N logarithmically-spaced values between xmin and xmax

double ccl_j_bessel(int l,double x);
//Spherical Bessel function of order l (adapted from CAMB)
