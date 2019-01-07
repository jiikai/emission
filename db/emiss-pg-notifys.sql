CREATE TABLE Notifier (i int4);

CREATE RULE year_notifyrule_insert AS ON INSERT TO YearData DO (INSERT INTO Notifier VALUES (1); NOTIFY Notifier, 'ok');

CREATE RULE year_notifyrule_update AS ON UPDATE TO YearData DO (INSERT INTO Notifier VALUES (2); NOTIFY Notifier, 'ok');

CREATE RULE country_notifyrule_insert AS ON INSERT TO Country DO (INSERT INTO Notifier VALUES (3); NOTIFY Notifier, 'ok');

CREATE RULE country_notifyrule_update AS ON UPDATE TO Country DO (INSERT INTO Notifier VALUES (4); NOTIFY Notifier, 'ok');

/* Either: */

CREATE RULE co2e_notifyrule_insert AS ON INSERT TO CO2EmissionData DO (INSERT INTO Notifier VALUES (5); NOTIFY Notifier, 'ok');

CREATE RULE co2e_notifyrule_update AS ON UPDATE TO CO2EmissionData DO (INSERT INTO Notifier VALUES (6); NOTIFY Notifier, 'ok');

CREATE RULE population_notifyrule_insert AS ON INSERT TO PopulationData DO (INSERT INTO Notifier VALUES (7); NOTIFY Notifier, 'ok');

CREATE RULE population_notifyrule_update AS ON UPDATE TO PopulationData DO (INSERT INTO Notifier VALUES (8); NOTIFY Notifier, 'ok');

/* Or alternatively: */

CREATE RULE datapoint_notifyrule_insert AS ON INSERT TO Datapoint DO (INSERT INTO Notifier VALUES (5); NOTIFY Notifier, 'ok');

CREATE RULE datapoint_notifyrule_update AS ON UPDATE TO Datapoint DO (INSERT INTO Notifier VALUES (6); NOTIFY Notifier, 'ok');
