/* Manually entered rows: */

INSERT INTO Region (name) VALUES ('East Asia & Pacific');
INSERT INTO Region (name) VALUES ('Europe & Central Asia');
INSERT INTO Region (name) VALUES ('Latin America & Caribbean');
INSERT INTO Region (name) VALUES ('Middle East & North Africa');
INSERT INTO Region (name) VALUES ('North America');
INSERT INTO Region (name) VALUES ('South Asia');
INSERT INTO Region (name) VALUES ('Sub-Saharan Africa');

INSERT INTO IncomeGroup (ordinal, name) VALUES (1, 'Low income');
INSERT INTO IncomeGroup (ordinal, name) VALUES (2, 'Lower middle income');
INSERT INTO IncomeGroup (ordinal, name) VALUES (3, 'Higher middle income');
INSERT INTO IncomeGroup (ordinal, name) VALUES (4, 'High income');

/* CO2E.KT */

    INSERT INTO YearData (year)
        VALUES (%d);

    INSERT INTO Country (code, name)
        VALUES ('%s', '%s');

    /* FOR WORLD AGGREGATE STATISTIC ROW - CODE "WLD": */

    UPDATE YearData SET world_co2emissions=%f WHERE year=%d;

    INSERT INTO CO2EmissionData (country_code, yeardata_year, emission_kt)
        VALUES ('%s', %d, %f);

/* POP.TOTL */

    INSERT INTO PopulationData (country_code, yeardata_year, population_total)
    VALUES ('%s', %d, %d);

    /* FOR WORLD AGGREGATE STATISTIC ROW - CODE "WLD": */

    UPDATE YearData SET world_population=%d WHERE year=%d;

/* METADATA: CO2E.KT */
    /* for aggregates */
    UPDATE Country SET is_an_aggregate=%s, metadata='%s' WHERE code=%s;
    /* for regular entries */
    UPDATE Country SET region_id=%s, income_id=%s WHERE code=%s;

    WITH region_t AS
        (SELECT id AS id FROM Region WHERE name='%s'),
    income_t AS
        (SELECT id AS id FROM IncomeGroup WHERE name='%s')
    UPDATE Country SET region_id=(SELECT id FROM region_t), income_id==(SELECT id FROM income_t), metadata='%s' WHERE code='%s';


INSERT INTO Country (code, name) VALUES ('JPN', 'Japan');

WITH region_t AS
    (SELECT id AS id FROM Region WHERE name='East Asia & Pacific'),
income_t AS
    (SELECT id AS id FROM IncomeGroup WHERE name='High')
UPDATE Country SET region_id=(SELECT id FROM region_t), income_id=(SELECT id FROM income_t), metadata='Fiscal year end: March 31; reporting period for national accounts data: CY.' WHERE code='JPN';
