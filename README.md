## Emission: Worldbank indicator data visualization [WIP]

### About

This repository contains the components of Emission, a Heroku app that creates charts out of retrieved [Worldbank data](http://api.worldbank.org/v2/en/indicator/). The app is deployed at https://emission.herokuapp.com.

All of the server-side functionality - including the fetching, parsing, uploading, updating and formatting of Worldbank data - is written in C, while the graphs are produced client-side by the [tui.chart](https://github.com/nhnent/tui.chart) JavaScript library.

### Documentation

See [API documentation](./doc/emiss_api.md). __TODO:__ documentation is partly outdated.

### Building ###

Run `make` in project root folder. This will create an executable file `emiss` in the `/bin` directory.

The following compilation options are provided. You can pass them to `make` as arguments: `make -D[option-name](=[option-value])`.

| Option          | Default value | Defined in | Description
|:--------------- |:--------------|:-----------|:-----------
|`HEROKU`         | undefined     |`emiss.h`   | Switch on Heroku-specific modifications

The application will look for __a [valid](https://www.postgresql.org/docs/current/libpq-connect.html#LIBPQ-CONNSTRING) Postgres database URL__ in an environment variable (or a config var in Heroku context) __`DATABASE_URL`__.

No system-wide installation option is currently provided.

### Project C files from `include` and `src`

- `emiss.h`: Main project header.
- `wlcsv.h`: A wrapper around [libcsv](#builtin-c-dependencies), making the association of multiple callbacks per csv parsing instance possible.
- `wlpq.h`: Strives to provide asynchronous, nonblocking PostgreSQL database querying facilities around [libpq](#builtin-c-dependencies).
- `util_json.h/util_sql.h/util_curl.h`: Auxiliary utility macros for formatting JSON, SQL and setting libcurl options, respectively.


### Embedded C dependency files from `include/dep` and `src/dep`

| Header files        | Description                                                                  | Source files   
|:--------------------|:-----------------------------------------------------------------------------|:--------------------------------------------------------------
|`bstrlib.h`          |[The Better String Library](https://github.com/msteinert/bstring)             |`bstrlib.c`
|`civetweb.h`         |[CivetWeb HTTP/S server](https://github.com/civetweb/civetweb)                |`civetweb.c` `handle_form.inl` `md5.inl` `sha1.inl` `timer.inl`
|`csv.h`              |[CSV data parsing library](https://github.com/rgamble/libcsv)                 |`libcsv.h`
|`miniz.h`            |[Header-only zlib/Deflate implementation](https://github.com/richgel999/miniz)|
|`uthash.h` `utlist.h`|[Hash table/linked list macro headers](https://github.com/troydhanson/uthash) |
|`zip.h`              |[A (de)compression library built on `miniz.h`](https://github.com/kuba--/zip) |`zip.c`

### External C dependencies (system-wide availability assumed)
| Header     | Description
|:-----------|:------------------------------------------------------------------------
|`curl.h`    |[libcurl](https://curl.haxx.se/libcurl/)
|`libpq-fe.h`|[PostgreSQL C library](https://www.postgresql.org/docs/10/libpq.html)
|`pcre.h`    |[PCRE regular expression library](http://www.pcre.org)


### Dependencies for Heroku app

| Project                                                 | Used for                                 | Delivery
|:--------------------------------------------------------|:-----------------------------------------|:--------------
|[Fontawesome](https://fontawesome.com)                   | Icons                                    |[CDN](https://use.fontawesome.com/releases/v5.6.3/css/all.css)
|[HTML5 Boilerplate CSS](https://html5boilerplate.com)    | App CSS incorporates some utility classes|[Local, concatenated to project stylesheet](./resources/css/all.min.css)
|[jQuery Slim](https://jquery.com)                        | The chart paramer input functionality    |[CDN](https://code.jquery.com/jquery-3.3.1.slim.min.js)
|[Luxbar](https://balzss.github.io/luxbar/)               | CSS navbar implementation                |[Local, concatenated to project stylesheet](./resources/css/luxbar.min.css)
|[Normalize.css](https://necolas.github.io/normalize.css/)| Cross-browser style normalization.       |[Local, concatenated to project stylesheet](./resources/css/normalize.min.css)
|[tui.chart](https://github.com/nhnent/tui.chart)         | Client side data visualization           |[CDN](https://uicdn.toast.com/tui.chart/latest/tui-chart-all.min.js)
|[Verge](http://verge.airve.com)                          | Viewport dimensions detection            |[Local](./resources/js/verge.min.js)


### Future directions ###

- Replace the DIY event loops from `wlpq.h` with something like [libev](http://software.schmorp.de/pkg/libev.html). Perhaps get rid of pthreads altogether.
- Create a more flexible update mechanism.
- Separate the data retrieval and parsing facilities from `emiss.h` into their own interface, opening avenues for general querying of indicator data from Worldbank.
- Replace the naive substring search in `param.js` with a suffix array or somesuch device.
- Add more chart types to the Heroku app.
- Get rid of the remaining jQuery.

### License ###

(c) Joa Käis [jiikai](https://github.com/jiikai) 2018-2019, [MIT](LICENSE).
