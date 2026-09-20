-- Generate the sibling DDL before running this probe:
-- tbe_compiler test_postgresql_security_probe.schema --lang postgresql \
--   --output test_postgresql_security_probe.generated.sql
-- psql -X --set ON_ERROR_STOP=1 --file test_postgresql_security_probe_verify.sql

\set ON_ERROR_STOP on

SET standard_conforming_strings = off;
CREATE TABLE protected (marker text NOT NULL);

\ir test_postgresql_security_probe.generated.sql

INSERT INTO defaults DEFAULT VALUES;

DO $verify$
DECLARE
  actual_payload bytea;
  actual_signed integer;
  actual_decimal real;
  actual_exponent double precision;
BEGIN
  SELECT convert_to(payload, 'UTF8'), signed_default, decimal_default, exponent_default
    INTO actual_payload, actual_signed, actual_decimal, actual_exponent
    FROM defaults;

  IF actual_payload <> decode(
      '736166655c273b2044524f50205441424c452070726f7465637465643b202d2d',
      'hex') THEN
    RAISE EXCEPTION 'payload bytes differ: %', encode(actual_payload, 'hex');
  END IF;
  IF actual_signed <> -1 OR actual_decimal <> 1.25 OR actual_exponent <> 1e-3 THEN
    RAISE EXCEPTION 'numeric defaults differ: %, %, %',
      actual_signed, actual_decimal, actual_exponent;
  END IF;
  IF to_regclass('public.protected') IS NULL THEN
    RAISE EXCEPTION 'injection-shaped default executed SQL';
  END IF;
END
$verify$;

SELECT current_setting('standard_conforming_strings') AS standard_conforming_strings,
       encode(convert_to(payload, 'UTF8'), 'hex') AS payload_hex,
       signed_default,
       decimal_default,
       exponent_default,
       to_regclass('public.protected') IS NOT NULL AS protected_exists
  FROM defaults;
