#include <string.h>
#include <stdio.h>
#include <fitsio2.h>
#include <iostream>

int main (int argc, char *argv[])
{
  if (argc < 2)
  {
    std::cout << "Usage: " << argv[0] << " m81.fits" << std::endl;
    return EXIT_FAILURE;
  }
  fitsfile *fptr;    /* FITS file pointers defined in fitsio.h */
  char card[FLEN_CARD];   /* Standard string lengths defined in fitsio.h */
  int status = 0;
  int single = 0, hdupos, nkeys, ii;

  /* Open the input file */
if (!fits_open_file(&fptr, argv[1], READONLY, &status))
    {
      fits_get_hdu_num(fptr, &hdupos);  /* Get the current HDU position */

      /* List only a single header if a specific extension was given */ 
      if (hdupos != 1 || strchr(argv[1], '[')) single = 1;

      for (; !status; hdupos++)  /* Main loop through each extension */
      {
        fits_get_hdrspace(fptr, &nkeys, NULL, &status); /* get # of keywords */

        printf("Header listing for HDU #%d:\n", hdupos);

        for (ii = 1; ii <= nkeys; ii++) { /* Read and print each keywords */

           if (fits_read_record(fptr, ii, card, &status))break;
           printf("%s\n", card);
        }
        printf("END\n\n");  /* terminate listing with END */

        if (single) break;  /* quit if only listing a single header */

        fits_movrel_hdu(fptr, 1, NULL, &status);  /* try to move to next HDU */
      }

      if (status == END_OF_FILE)  status = 0; /* Reset after normal error */

      fits_close_file(fptr, &status);
    }

    if (status) fits_report_error(stderr, status); /* print any error message */
    return(status);
}