%module ccl_lsst_specs

%{
/* put additional #include here */
#include "../include/ccl_params.h"
#include "../include/ccl_lsst_specs.h"
%}

// Enable vectorised arguments for arrays
%apply (double* IN_ARRAY1, int DIM1) {
        (double* a, int na),
        (double* z, int nz)
};
%apply (int DIM1, double* ARGOUT_ARRAY1) {(int nout, double* output)};

%include "../include/ccl_lsst_specs.h"

%feature("pythonprepend") specs_bias_clustering_vec %{
    if numpy.shape(a) != (nout,):
        raise CCLError("Input shape for `a` must match `(nout,)`!")
%}

%inline %{

void specs_bias_clustering_vec(ccl_cosmology * cosmo, double* a, int na,
                               int nout, double* output, int* status) {
    for(int i=0; i < na; i++) {
        output[i] = ccl_specs_bias_clustering(cosmo, a[i], status);
    }
}

%}

/* Applies to all functions below. */
%feature("pythonprepend") %{
    if numpy.shape(z) != (nout,):
        raise CCLError("Input shape for `z` must match `(nout,)`!")
%}

%inline %{

void specs_sigmaz_clustering_vec(double* z, int nz, int nout, double* output) {
    for(int i=0; i < nz; i++) {
        output[i] = ccl_specs_sigmaz_clustering(z[i]);
    }
}

void specs_sigmaz_sources_vec(double* z, int nz, int nout, double* output) {
    for(int i=0; i < nz; i++) {
        output[i] = ccl_specs_sigmaz_sources(z[i]);
    }
}


// Vectorised version of ccl_specs_dNdz_tomog()
void specs_dNdz_tomog_vec(int dNdz_type, double bin_zmin, double bin_zmax,
                          user_pz_info* user_info, double* z, int nz,
                          int nout, double* output, int *status) {
    double val = 0.;

    // Loop over z values
    for(int i=0; i < nz; i++) {

        // Calculate dNdz value
        ccl_specs_dNdz_tomog(z[i], dNdz_type, bin_zmin, bin_zmax,
			     user_info, &val, status);
        // Check status
        if (*status != 0){
            fprintf(stderr, "%s",
                    "specs_dNdz_tomog_vec: You have selected an unsupported "
                    "dNdz type. Exiting.\n");
            return;
        } // End status check

        // Add return value to output array
        output[i] = val;

    } // End loop over z values

    return;
}

%}

/* The directive gets carried between files, so we reset it at the end. */
%feature("pythonprepend") %{ %}

%inline %{

// C callback function, to call a pre-specified Python function with
// call signature def fn(double, double): return double
static double call_py_photoz_fn(double z_ph, double z_s, void *py_func_obj, int *status)
{
    PyObject *func, *arglist;
    PyObject *result;
    double p;

    // Ensure we are holding the GIL
    PyGILState_STATE gil_state = PyGILState_Ensure();

    // Get Python function object and construct argument list
    func = (PyObject *) py_func_obj;
    arglist = Py_BuildValue("dd", z_ph, z_s); // Assumed to take two double args

    // Call Python function and dereference argument list
    result = PyObject_CallObject(func, arglist); // PyEval_CallObject
    Py_DECREF(arglist);

    // Check result; raise error if the function call failed
    if(result){
        p = PyFloat_AsDouble(result);
    }else{
        fprintf(stderr, "call_py_photoz_fn: Call to Python function failed.\n");
        exit(1);
    }
    Py_XDECREF(result);

    // Release the GIL and return result
    PyGILState_Release(gil_state);
    return p;
}

// Python wrapper for ccl_specs_create_photoz_info(); takes a Python function
// object and attaches a callable C function to it that CCL can call directly
user_pz_info* specs_create_photoz_info_from_py(PyObject *pyfunc)
{
    // Check that input Python object is callable
    if (!PyCallable_Check(pyfunc)) {
        PyErr_SetString(PyExc_TypeError, "Argument must be a callable function.");
        return NULL;
    }

    // Set up callback to call_py_photoz_fn(), with the user_params
    // set to be a pointer to the Python function object itself
    user_pz_info* pzinfo = ccl_specs_create_photoz_info( (void*)pyfunc,
                                                         &call_py_photoz_fn );
    Py_XINCREF(pyfunc);
    return pzinfo;
}

%}
