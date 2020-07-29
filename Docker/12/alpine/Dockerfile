FROM postgres:12-alpine AS build

RUN apk --update add \
	alpine-sdk \
	postgresql-dev \
  && cd /root \
  && git clone --depth 1 https://github.com/mdgomes/pgsphere.git \
  && cd pgsphere \
  && make USE_PGXS=1 PG_CONFIG=/usr/bin/pg_config \
  && make USE_PGXS=1 PG_CONFIG=/usr/bin/pg_config install

FROM postgres:12-alpine

COPY --from=build /usr/lib/postgresql/bitcode/pg_sphere /usr/lib/postgresql/bitcode/pg_sphere

ENTRYPOINT ["docker-entrypoint.sh"]

EXPOSE 5432
CMD ["postgres"]
