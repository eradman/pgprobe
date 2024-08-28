CREATE OR REPLACE FUNCTION rule_notify()
  RETURNS trigger AS
$$
  BEGIN
    PERFORM pg_notify('rule_channel', NEW.name);
    RETURN NULL;
  END;
$$ LANGUAGE plpgsql;;

CREATE TRIGGER rule_notify
AFTER INSERT OR UPDATE OR DELETE ON probe_rules
FOR EACH ROW EXECUTE PROCEDURE rule_notify();
