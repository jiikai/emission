/*	Tables needed for the database. Currently entered manually. */

CREATE TABLE IncomeGroup (
	id SERIAL PRIMARY KEY,
	ordinal integer UNIQUE,
	name text UNIQUE
);

CREATE TABLE Region (
	id SERIAL PRIMARY KEY,
	name text UNIQUE
);

CREATE TABLE YearData (
	year integer PRIMARY KEY,
	world_population bigint,
	world_co2emissions real,
	notes text
);

CREATE TABLE Country (
	code_iso_a3 varchar(3) PRIMARY KEY,
	code_iso_a2 varchar(2) UNIQUE,
	region_id integer REFERENCES Region(id),
	income_id integer REFERENCES IncomeGroup(id),
	name text UNIQUE,
	is_independent boolean,
	is_an_aggregate boolean,
	in_tui_chart boolean,
	metadata text
);

CREATE TABLE Datapoint (
	id SERIAL PRIMARY KEY,
	country_code varchar(3) REFERENCES Country(code_iso_a3),
	yeardata_year integer REFERENCES YearData(year),
	emission_kt double precision,
	population_total bigint,
	UNIQUE (country_code, yeardata_year)
);

CREATE TABLE DataUpdate (
	tmstmp timestamp PRIMARY KEY DEFAULT CURRENT_TIMESTAMP,
	checked boolean,
	run boolean
);
