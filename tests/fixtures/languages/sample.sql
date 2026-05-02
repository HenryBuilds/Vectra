-- Minimal SQL fixture for language parse-validation tests.

CREATE TABLE users (
    id BIGSERIAL PRIMARY KEY,
    label TEXT NOT NULL,
    created_at TIMESTAMPTZ DEFAULT now()
);

CREATE INDEX idx_users_label ON users (label);

CREATE VIEW recent_users AS
    SELECT id, label
    FROM users
    WHERE created_at > now() - interval '7 days';

CREATE FUNCTION greet(name TEXT) RETURNS TEXT AS $$
    SELECT 'Hello, ' || name;
$$ LANGUAGE SQL IMMUTABLE;
