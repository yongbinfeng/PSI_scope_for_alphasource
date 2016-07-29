/*
   Purpose:        Example file to read binary data saved by DRSOsc, 
                   calculate the average pulse around the peak of 
                   the energy/area distribution and save it as text.
*/

#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <math.h>

#define VOLTCUTMIN 5.2
#define VOLTCUTMAX 5.4

typedef struct {
   char           tag[3];
   char           version;
} FHEADER;

typedef struct {
   char           time_header[4];
} THEADER;

typedef struct {
   char           bn[2];
   unsigned short board_serial_number;
} BHEADER;

typedef struct {
   char           event_header[4];
   unsigned int   event_serial_number;
   unsigned short year;
   unsigned short month;
   unsigned short day;
   unsigned short hour;
   unsigned short minute;
   unsigned short second;
   unsigned short millisecond;
   unsigned short range;
} EHEADER;

typedef struct {
   char           tc[2];
   unsigned short trigger_cell;
} TCHEADER;

typedef struct {
   char           c[1];
   char           cn[3];
} CHEADER;

/*-----------------------------------------------------------------------------*/

int main(int argc, const char * argv[])
{
   FHEADER  fh;
   THEADER  th;
   BHEADER  bh;
   EHEADER  eh;
   TCHEADER tch;
   CHEADER  ch;
   
   unsigned int scaler;
   unsigned short voltage[1024];
   double waveform[16][4][1024], time[16][4][1024];
   float bin_width[16][4][1024];
   double sumwaveform[2001]={0};
   int i, j, b, chn, n, chn_index, n_boards, Ncount;
   double t1, t2, dt;
   char filename[256];

   int ndt;
   double threshold, sumdt, sumdt2;
   double area;

   if (argc > 1)
      strcpy(filename, argv[1]);
   else {
      printf("Usage: read_binary <filename>\n");
      return 0;
   }
   
   // open the binary waveform file
   FILE *f = fopen(filename, "r");
   if (f == NULL) {
      printf("Cannot find file \'%s\'\n", filename);
      return 0;
   }

   // read file header
   fread(&fh, sizeof(fh), 1, f);
   if (fh.tag[0] != 'D' || fh.tag[1] != 'R' || fh.tag[2] != 'S') {
      printf("Found invalid file header in file \'%s\', aborting.\n", filename);
      return 0;
   }
   
   if (fh.version != '2') {
      printf("Found invalid file version \'%c\' in file \'%s\', should be \'2\', aborting.\n", fh.version, filename);
      return 0;
   }

   // read time header
   fread(&th, sizeof(th), 1, f);
   if (memcmp(th.time_header, "TIME", 4) != 0) {
      printf("Invalid time header in file \'%s\', aborting.\n", filename);
      return 0;
   }

   for (b = 0 ; ; b++) {
      // read board header
      fread(&bh, sizeof(bh), 1, f);
      if (memcmp(bh.bn, "B#", 2) != 0) {
         // probably event header found
         fseek(f, -4, SEEK_CUR);
         break;
      }
      
      //printf("Found data for board #%d\n", bh.board_serial_number);
      
      // read time bin widths
      memset(bin_width[b], sizeof(bin_width[0]), 0);
      for (chn=0 ; chn<5 ; chn++) {
         fread(&ch, sizeof(ch), 1, f);
         if (ch.c[0] != 'C') {
            // event header found
            fseek(f, -4, SEEK_CUR);
            break;
         }
         i = ch.cn[2] - '0' - 1;
         //printf("Found timing calibration for channel #%d\n", i+1);
         fread(&bin_width[b][i][0], sizeof(float), 1024, f);
         // fix for 2048 bin mode: double channel
         if (bin_width[b][i][1023] > 10 || bin_width[b][i][1023] < 0.01) {
            for (j=0 ; j<512 ; j++)
               bin_width[b][i][j+512] = bin_width[b][i][j];
         }
      }
   }
   n_boards = b;

   // initialize statistics
   ndt = 0;
   sumdt = sumdt2 = 0;
   
   printf("BinNo.\tTime/ns\tVoltage/V\n");  
 
   // loop over all events in the data file
   for (n=0 ; ; n++) {
      // read event header
      i = (int)fread(&eh, sizeof(eh), 1, f);
      if (i < 1)
         break;
      
      //printf("%d\t ", eh.event_serial_number);
 
      // loop over all boards in data file
      for (b=0 ; b<n_boards ; b++) {
         // read board header
         fread(&bh, sizeof(bh), 1, f);
         if (memcmp(bh.bn, "B#", 2) != 0) {
            printf("Invalid board header in file \'%s\', aborting.\n", filename);
            return 0;
         }
         
         // read trigger cell
         fread(&tch, sizeof(tch), 1, f);
         if (memcmp(tch.tc, "T#", 2) != 0) {
            printf("Invalid trigger cell header in file \'%s\', aborting.\n", filename);
            return 0;
         }

         if (n_boards > 1)
            printf("Found data for board #%d\n", bh.board_serial_number);
         
         // reach channel data
         for (chn=0 ; chn<4 ; chn++) {
            // read channel header
            fread(&ch, sizeof(ch), 1, f);
            if (ch.c[0] != 'C') {
               // event header found
               fseek(f, -4, SEEK_CUR);
               break;
            }
            chn_index = ch.cn[2] - '0' - 1;
            fread(&scaler, sizeof(int), 1, f);
            // printf("Scaler is %u\n", scaler);
	    fread(voltage, sizeof(short), 1024, f);
            
	    //printf("eh.range is %d\n", eh.range);

            for (i=0 ; i<1024 ; i++) {
               // convert data to volts
               waveform[b][chn_index][i] = (voltage[i] / 65536. + eh.range/1000.0 - 0.5);
               // calculate time for this cell
               for (j=0,time[b][chn_index][i]=0 ; j<i ; j++)
                  time[b][chn_index][i] += bin_width[b][chn_index][(j+tch.trigger_cell) % 1024];
            }
         }
         
         // align cell #0 of all channels
         t1 = time[b][0][(1024-tch.trigger_cell) % 1024];
         for (chn=1 ; chn<4 ; chn++) {
            t2 = time[b][chn][(1024-tch.trigger_cell) % 1024];
            dt = t1 - t2;
            for (i=0 ; i<1024 ; i++)
               time[b][chn][i] += dt;
         }
	 
         /*-----------------------------------------------------------------------------*/
	 // me: Get the area of the pulse
         double area = 0.;
         double min = waveform[b][0][0];
         int ibin;

         for (i=0; i<1023; i++) {
            if( waveform[b][0][i+1]<min ) min = waveform[b][0][i+1];
            double minivolt = (waveform[b][0][i+1] + waveform[b][0][i])/2;
            double minitime = time[b][0][i+1] - time[b][0][i];
            double miniarea = minivolt*minitime;
            area = area + miniarea;
         }
         
         if(-area>VOLTCUTMIN && -area<VOLTCUTMAX && min>-0.4999){
	    Ncount++; 
            for (i=0; i<1024; i++) {
	        ibin = int(time[b][0][i]*10);
                sumwaveform[ibin] += waveform[b][0][i]; 
	    }
         }
      }
   }

   /*---------------------------------*/
   // calculate the average
   printf("###Number of Events within the area between %.1lf and %.1lf is %d\n", VOLTCUTMIN, VOLTCUTMAX, Ncount);
   for( i=0; i<2001; i++){
      double avgwaveform = sumwaveform[i]/Ncount;
      printf("%d\t%lf\t%lf\n", i+1, 0.1*i+0.05, -avgwaveform);
   }  

   return 1;
}
