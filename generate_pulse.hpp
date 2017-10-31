#include <iostream>
#include <cstdlib>
#include <fstream>
#include <cmath>
#include <cstring>

template<class T> 
char* as_bytes(T& i)
{ 
  void* addr = &i;
  return static_cast<char *>(addr);
}

long double dmdelay(long double f1, long double f2, float dm, float tsamp) /* includefile */
{
  return(4148.741601*((1.0/f1/f1)-(1.0/f2/f2))*dm/tsamp);
}


class send_stuff
{
public:
  
  int send_string(std::string text, std::ofstream& fpout)
  {
    int len;
    len = (int)text.size();
    fpout.write(as_bytes(len), sizeof(int));
    fpout.write(text.c_str(), text.size());
    return 0;
  }
  
  int send_double(std::string text, double value,  std::ofstream& fpout)
  {
    send_string(text,fpout);
    fpout.write(as_bytes(value),sizeof(double));
		return 0;
  }  
  
  int send_int(std::string text, int value, std::ofstream& fpout)
  {
    send_string(text,fpout);
    fpout.write(as_bytes(value),sizeof(int));
		return 0;
  }
  
  void send_coords(double raj, double dej, double az, double za,std::ofstream
& fpout) /*includefile*/
  {
    if ((raj != 0.0) || (raj != -1.0)) send_double("src_raj",raj, fpout);
    if ((dej != 0.0) || (dej != -1.0)) send_double("src_dej",dej, fpout);
    if ((az != 0.0)  || (az != -1.0))  send_double("az_start",az, fpout);
    if ((za != 0.0)  || (za != -1.0))  send_double("za_start",za, fpout);
  }
};
  
class filterbank:send_stuff
{
  public:
    int obits;
    double src_raj,src_dej,az_start,za_start;
    double fch1;
    double foff;
    int nchans;
    int nbeams;
    int ibeam;
    double tstart;
    double start_time;
    double tsamp;
    int machine_id;
    int telescope_id;
    float *fil_data;
    int nacc; 
    filterbank(int nbands, int n)
    {
      nacc = n;
      obits=32;
      src_raj=0.0;
      src_dej=0.0;
      az_start=0.0;
      za_start=0.0;
      fch1 = 800;
      foff = -1*0.0244140625;
      nchans = nbands;
      nbeams = 1;
      ibeam = 1;
      tstart = 57227.5;
      start_time = 0.0;
      tsamp = 0.00098304;  
      machine_id = 7;
      telescope_id = 2;
      
    }
    
    char* read_string(std::ifstream& fpin)
    {
      int len;
      fpin.read(as_bytes(len),sizeof(int));
      char *temp;
      temp = new char[len];
      fpin.read(as_bytes(temp[0]),len);
      return temp;
    }
    
    int read_header(std::ifstream& fpin)
    {
      int data_type,nifs;
      double dummy;
      std::cout<<"reading header \n";
      if(std::strcmp(read_string(fpin),"HEADER_START")==0)
      {
        std::cout<<"well looks like SIGPROC filterbank format\n";
      }
      char *temp;
      while(strcmp(temp,"HEADER_END")!=0)
      {
        temp = read_string(fpin);
        if(strcmp(temp,"machine_id")==0)
        {
          fpin.read(as_bytes(machine_id),sizeof(int));
        }
        if(strcmp(temp,"telescope_id")==0)
        {
          fpin.read(as_bytes(telescope_id),sizeof(int));
        }
        if(strcmp(temp,"src_raj")==0)
        {
          fpin.read(as_bytes(dummy),sizeof(double));
        }
        if(strcmp(temp,"src_dej")==0)
        {
          fpin.read(as_bytes(dummy),sizeof(double));
        }
        if(strcmp(temp,"az_start")==0)
        {
          fpin.read(as_bytes(dummy),sizeof(double));
        }
        if(strcmp(temp,"za_start")==0)
        {
          fpin.read(as_bytes(dummy),sizeof(double));
        } 
        if(strcmp(temp,"data_type")==0)
        {
          fpin.read(as_bytes(data_type),sizeof(int));
        }
        if(strcmp(temp,"fch1")==0)
        {
          fpin.read(as_bytes(fch1),sizeof(double));
        }
        if(strcmp(temp,"foff")==0)
        {
          fpin.read(as_bytes(foff),sizeof(double));
        }    
        if(strcmp(temp,"nchans")==0)
        {
          fpin.read(as_bytes(nchans),sizeof(int));
        }
        if(strcmp(temp,"nbeams")==0)
        {
          fpin.read(as_bytes(nbeams),sizeof(int));
        }
        if(strcmp(temp,"ibeam")==0)
        {
          fpin.read(as_bytes(ibeam),sizeof(int));
        }
        if(strcmp(temp,"nbits")==0)
        {
          fpin.read(as_bytes(obits),sizeof(int));
        }
        if(strcmp(temp,"tstart")==0)
        {
          fpin.read(as_bytes(tstart),sizeof(double));
        }
        if(strcmp(temp,"tsamp")==0)
        {
          fpin.read(as_bytes(tsamp),sizeof(double));
        }
        if(strcmp(temp,"nifs")==0)
        {
          fpin.read(as_bytes(nifs),sizeof(int));
        }
      } 
      return 0;
    }
    int write_header(std::ofstream& fpout)
    { 
      /* broadcast the header parameters to the output stream */
      send_string("HEADER_START",fpout);
      send_string("rawdatafile",fpout);
      send_string("test.dat",fpout);
      send_int("machine_id",machine_id,fpout);
      send_int("telescope_id",telescope_id,fpout);
      send_coords(src_raj,src_dej,az_start,za_start,fpout);
      send_int("data_type",1,fpout);
      send_double("fch1",fch1,fpout);
      send_double("foff",foff,fpout);
      send_int("nchans",nchans,fpout);
     
      /* beam info */
      send_int("nbeams",nbeams,fpout);
      send_int("ibeam",ibeam,fpout);
      /* number of bits per sample */
      send_int("nbits",obits,fpout);
      /* start time and sample interval */
      send_double("tstart",tstart+(double)start_time/86400.0,fpout);
      send_double("tsamp",tsamp,fpout);
      send_int("nifs",1,fpout);
      send_string("HEADER_END",fpout);
      return 0;
    }
   
    
    /*
    int write(std::ofstream& fpout, float *data, int nsamples, int nbands)
    {
      
      for(int sample = 0; sample<nsamples; sample++)
      {
        for(int channel=0;channel<nbands;channel++)
        {
          fil_data[sample*nbands+channel] = (short)(data[(nbands-1-channel)*n
samples+sample]/5000000);
          if(sample ==10 && channel == 23) std::cout<<"values "<< (data[(nban
ds-1-channel)*nsamples+sample])/5000000 <<std::endl;
        }
      }
      
      fpout.write(as_bytes(fil_data[0]),sizeof(short)*nsamples*nbands);
      fpout.flush();
    }
    */
    
};



class sim_pulse
{
	public:
	int nsamples = 1024;
	float tsamp = 0.00098304;
	float *data = new float[1024];
	long *shifts = new long[16*1024];
	float width;
	float snr;

	long start;
  

	sim_pulse(float w, float s, float location, float dm)
	{
	  width = w;
		snr = s/std::sqrt(w*16*1024);
		start = (long)((long double)location/(long double)tsamp) - 512;
    
		for(int i=0;i<1024;i++)
		{
			data[i] = snr*std::exp(-1*std::pow(i-512,2)/(2*std::pow(width,2)));
		  //std::cout<<data[i]<<"\n";	
    }

		for(int i=0; i<16*1024;i++)
		{
			long double foff = 400.0/(16*1024);
			long double f1 = 800-i*foff;
			long double delay = dmdelay(f1,800.0,1.0,tsamp);
			shifts[i] = (long)(delay*(long double)dm);
	  }	
  }
  
	int reinitialize(float w, float s, float location, float dm)
	{
	  width = w;
		snr = s/std::sqrt(w*16*1024);
		start = (long)((long double)location/(long double)tsamp) - 512;

		for(int i=0;i<1024;i++)
		{
		  data[i] = snr*std::exp(-1*std::pow(i-512,2)/(2*std::pow(width,2)));
			//std::cout<<data[i]<<"\n"; 
		}
		
		for(int i=0; i<16*1024;i++)
		{
		  long double foff = 400.0/(16*1024);
			long double f1 = 800-i*foff;
			long double delay = dmdelay(f1,800.0,1.0,tsamp);
			shifts[i] = (long)(delay*(long double)dm);
		}
	}
																				
  int add_value(float *sample, long sample_number, int nchans)
	{
    for(int i=0;i<nchans;i++)
		{
			for(int n=0; n<16; n++)
			{
				long location = sample_number+n-shifts[i];
        if(location>=start && location<=(start+1023)) sample[i*16+n] += data[location-start];
		  }
		}	
		return 0;

	}
};

extern class sim_pulse sim0;
extern class sim_pulse sim1;
extern class sim_pulse sim2;
extern class sim_pulse sim3;















 
