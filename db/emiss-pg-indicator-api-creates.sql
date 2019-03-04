/*  These are for possible future use. */

CREATE TABLE Indicator (
    id PRIMARY KEY,
    source_id integer REFERENCES Source(id),
    name text,
    unit text
);

CREATE TABLE Source (
    id integer PRIMARY KEY,
    name text,
    note text,
    organisation text
);

CREATE TABLE Topic (
    id integer PRIMARY KEY,
    name text
);

CREATE TABLE IndicatorTopic (
    id SERIAL PRIMARY KEY,
    indicator_id integer REFERENCES Indicator(id),
    topic_id integer REFERENCES Topic(id)
);
