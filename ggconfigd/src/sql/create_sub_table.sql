CREATE TEMPORARY TABLE subscriberTable (
  'keyid' INT NOT NULL,
  'handle' INT,
  FOREIGN KEY (keyid) REFERENCES keyTable (keyid)
)
