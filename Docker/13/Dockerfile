FROM postgres:13 AS build

RUN apt-get update \
&& apt-get install -y \
	ca-certificates \
	make \
	git \
	gcc \
	bison \
	postgresql-server-dev-13 \
&& cd /root \
&& git clone --depth 1 https://github.com/mdgomes/pgsphere.git \
&& cd pgsphere \
&& make USE_PGXS=1 PG_CONFIG=/usr/bin/pg_config \
&& make USE_PGXS=1 PG_CONFIG=/usr/bin/pg_config install 


FROM postgres:13

COPY --from=build /usr/lib/postgresql/13/lib/bitcode/pg_sphere /usr/lib/postgresql/13/lib/bitcode/pg_sphere

ENTRYPOINT ["docker-entrypoint.sh"]

EXPOSE 5432
CMD ["postgres"]
