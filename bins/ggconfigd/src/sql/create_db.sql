CREATE TABLE keyTable (
  'keyid' INTEGER PRIMARY KEY AUTOINCREMENT UNIQUE NOT NULL,
  'keyvalue' TEXT NOT NULL COLLATE BINARY
);

CREATE TABLE relationTable (
  'keyid' INT UNIQUE NOT NULL,
  'parentid' INT NOT NULL,
  PRIMARY KEY (keyid),
  FOREIGN KEY (keyid) REFERENCES keyTable (keyid),
  FOREIGN KEY (parentid) REFERENCES keyTable (keyid)
);

CREATE TABLE valueTable (
  'keyid' INT UNIQUE NOT NULL,
  'value' TEXT NOT NULL,
  'timeStamp' INTEGER NOT NULL,
  FOREIGN KEY (keyid) REFERENCES keyTable (keyid)
);

CREATE TABLE version ('version' TEXT DEFAULT '0.1');

INSERT INTO
  version (version)
VALUES
  (0.1);
