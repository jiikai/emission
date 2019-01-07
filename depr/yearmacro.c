#define EMISS_EXCLUDE_YEAR_RANGE(out)\
do {\
    char *mille, *cent, *dec_bfr, *year_bfr, *dec_ftr, *year_ftr;\
    if (EMISS_YEAR_ZERO > 1999) {\
        mille = "1";\
        cent = "9";\
    } else if (EMISS_YEAR_LAST < 2000) {\
        mille = "2";\
        cent = "0"\
    } else {\
        mille = "12";\
        cent = "09";\
    }\
    int rem;\
    if (EMISS_YEAR_ZERO < 2000) {\
        rem = EMISS_YEAR_ZERO - 1900;\
        if (rem < 70) {\
            dec_bfr = "6";\
        } else if (rem < 80) {\
            dec_bfr = "67";\
        } else if (rem < 90) {\
            dec_bfr = "6-8";\
        } else {\
            dec_bfr = "6-9"\
        }\
    } else {\
        rem = EMISS_YEAR_ZERO - 2000;
        if (EMISS_YEAR)
    }\
} while (0)
