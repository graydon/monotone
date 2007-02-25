#! /bin/sh

: ${MTN:=mtn}

for arg in "$@"; do
  version=$("$MTN" -d "$arg" db version; echo $?)
  case "$version" in
    *2881277287f6ee9bfc5ee255a503a6dc20dd5994*0)
      # this is the problem version
      echo "$0: fixing $arg" >&2
      "$MTN" -d "$arg" db execute '
        CREATE TABLE manifests
        (
        id primary key,      -- strong hash of all the entries in a manifest
        data not null        -- compressed, encoded contents of a manifest
        )'

      "$MTN" -d "$arg" db execute '
        CREATE TABLE manifest_deltas
        (
        id not null,      -- strong hash of all the entries in a manifest
        base not null,    -- joins with either manifest.id or manifest_deltas.id
        delta not null,   -- rdiff to construct current from base
        unique(id, base)
        )'

      "$MTN" -d "$arg" db execute '
        CREATE TABLE manifest_certs
        (
        hash not null unique,   -- hash of remaining fields separated by ":"
        id not null,            -- joins with manifests.id or manifest_deltas.id
        name not null,          -- opaque string chosen by user
        value not null,         -- opaque blob
        keypair not null,       -- joins with public_keys.id
        signature not null,     -- RSA/SHA1 signature of "[name@id:val]"
        unique(name, id, value, keypair, signature)
        )';;

    *0)
      echo "$0: skipping $arg (not the problem version)" >&2
      ;;
    *)
      # they will already have gotten an error message
      ;;
  esac
done
