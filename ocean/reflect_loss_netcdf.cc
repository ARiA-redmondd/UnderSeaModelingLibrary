/**
 * @file reflect_loss_netcdf.cc
 * Models plane wave reflection from a bottom province profile.
 */
#include <usml/ocean/reflect_loss_netcdf.h>
#include <exception>

using namespace usml::ocean ;

reflect_loss_netcdf::reflect_loss_netcdf(const char* filename) {

	NcFile file( filename );
	NcVar *_lat = file.get_var("lat");                      ///< _lat : latitude in degrees
	NcVar *_lon = file.get_var("lon");                      ///< _lon : longitude in degrees
	NcVar *bot_num = file.get_var("type");                  ///< bot_num  : bottom province map
	NcVar *bot_speed = file.get_var("speed_ratio");         ///< bot_speed  : speed ratio of the province
	NcVar *bot_density = file.get_var("density_ratio");     ///< bot_density  : density ratio of the province
	NcVar *bot_atten = file.get_var("atten");               ///< bot_atten  : attenuation value for the province
	NcVar *bot_shear_speed = file.get_var("shear_speed");   ///< bot_shear_speed  : shear speed of the province
	NcVar *bot_shear_atten = file.get_var("shear_atten");   ///< bot_shear_atten  : shear attenuation of the province

    /** Gets the size of the dimensions to be used to create the data grid */
    int ncid, latid, lonid, provid;
    long latdim, londim, n_types;
    ncid = ncopen(filename, NC_NOWRITE);
	latid = ncdimid(ncid, "lat");
    ncdiminq(ncid, latid, 0, &latdim);
    lonid = ncdimid(ncid, "lon");
    ncdiminq(ncid, lonid, 0, &londim);
    provid = ncdimid(ncid, "num_types");
    ncdiminq(ncid, provid, 0, &n_types);

    /** Extracts the data for all of the variables from the netcdf file and stores them */
    double* latitude = new double[latdim];
        _lat->get(&latitude[0], latdim);
    double* longitude = new double[londim];
        _lon->get(&longitude[0], londim);
    double* prov_num = new double[latdim*londim];
        bot_num->get(&prov_num[0], latdim, londim);
    double* speed = new double[n_types];
        bot_speed->get(&speed[0], n_types);
    double* density = new double[n_types];
        bot_density->get(&density[0], n_types);
    double* atten = new double[n_types];
        bot_atten->get(&atten[0], n_types);
    double* shearspd = new double[n_types];
        bot_shear_speed->get(&shearspd[0], n_types);
    double* shearatten = new double[n_types];
        bot_shear_atten->get(&shearatten[0], n_types);

    /** Creates a sequence vector of axises that are passed to the data grid constructor */
    seq_vector* axis[2];
    double latinc = ( latitude[latdim-1] - latitude[0] ) / latdim ;
    axis[0] = new seq_linear(latitude[0], latinc, int(latdim));
    double loninc = ( longitude[londim-1] - longitude[0] ) / londim ;
    axis[1] = new seq_linear(longitude[0], loninc, int(londim));

#ifdef USML_DEBUG
    cout << "===============axis layout=============" << endl;
    cout << "lat first: " << latitude[0] << "\nlat last: " << latitude[latdim-1] << "\nlat inc: " << latinc <<  "\nnum elements: " << (*axis[0]).size() << endl;
    cout << "lat axis: " << *axis[0] << endl;
    cout << "lon first: " << longitude[0] << "\nlon last: " << longitude[londim-1] << "\nlon inc: " << loninc << endl;
    cout << "lon axis: " << *axis[1] << endl;
    cout << endl;
#endif

    /** Creates a data grid with the above assigned axises and populates the grid with the data from the netcdf file */
    province = new data_grid<double,2>(axis);
    unsigned index[2];
    for(int i=0; i<londim; i++) {
        for(int j=0; j<latdim; j++) {
            index[0] = j;
            index[1] = i;
            province->data(index, prov_num[i*latdim+j]);
        }
    }

#ifdef USML_DEBUG
    cout << "==========data grid=============" << endl;
    for(int i=0; i<londim; i++) {
        for(int j=0; j<latdim; j++) {
            index[0] = j;
            index[1] = i;
            cout << province->data(index) << ",";
            if(j == latdim-1){
                cout << endl;
            }
        }
    }
    cout << endl;
#endif

    /** Set the interpolation type to the nearest neighbor and restrict extrapolation */
    for(int i=0; i<2; i++){
        province->interp_type(i, GRID_INTERP_NEAREST);
        province->edge_limit(i, true);
    }

    /** Builds a vector of reflect_loss_rayleigh values for all bottom province numbers */
    for(int i=0; i<int(n_types); i++) {
        rayleigh.push_back( new reflect_loss_rayleigh( density[i], speed[i]/1500, atten[i], shearspd[i]/1500, shearatten[i] ) );
    }

#ifdef USML_DEBUG
    cout << "Sediment properties:" << endl;
    cout << "\t\tSand\t\tLimestone" << endl;
    cout << "density:\t" << density[0] << "\t\t" << density[1] << endl;
    cout << "speed:\t\t" << speed[0] << "\t\t" << speed[1] << endl;
    cout << "attenuation:\t" << atten[0] << "\t\t" << atten[1] << endl;
    cout << "shear speed:\t" << shearspd[0] << "\t\t" << shearspd[1] << endl;
    cout << "shear atten:\t" << shearatten[0] << "\t\t" << shearatten[1] << endl;
    cout << "province:\t" << 0 << "\t\t" << 1 << endl;
    cout << "rayleigh:\t" << rayleigh[0] << "\t" << rayleigh[1] << endl << endl;
#endif

	delete[] latitude;
	delete[] longitude;
	delete[] speed;
	delete[] density;
	delete[] atten;
	delete[] shearspd;
	delete[] shearatten;
	delete[] prov_num;

}

/** Creates a rayleigh reflection loss value for the bottom province number
 * at a specific location then computes the broadband reflection loss and phase change.
 *
 * @param location      Location at which to compute attenuation.
 * @param frequencies   Frequencies over which to compute loss. (Hz)
 * @param angle         Reflection angle relative to the normal (radians).
 * @param amplitude     Change in ray strength in dB (output).
 * @param phase         Change in ray phase in radians (output).
 *                      Phase change not computed if this is NULL.
 */
void reflect_loss_netcdf::reflect_loss(
    const wposition1& location,
    const seq_vector& frequencies, double angle,
    vector<double>* amplitude, vector<double>* phase) {

    double loc[2];
    loc[0] = location.latitude();
    loc[1] = location.longitude();

    unsigned prov = province->interpolate(loc);
    rayleigh[prov]->reflect_loss(location, frequencies, angle,
        amplitude );
}

/** Interates over the rayleigh reflection loss values
 * and deletes them.
 */
reflect_loss_netcdf::~reflect_loss_netcdf() {
	for(std::vector<reflect_loss_rayleigh*>::iterator iter=rayleigh.begin(); iter != rayleigh.end(); iter++) {
        delete *iter;
	}
}
