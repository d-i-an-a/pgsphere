FROM postgres:12 

RUN apt-get update \
&& apt-get install -y \
	ca-certificates \
	make \
	git \
	gcc \
	postgresql-server-dev-12 \
&& cd /root \
&& git clone --depth 1 https://github.com/mdgomes/pgsphere.git \
&& cd pgsphere \
&& make USE_PGXS=1 PG_CONFIG=/usr/bin/pg_config \
&& make USE_PGXS=1 PG_CONFIG=/usr/bin/pg_config install 

ENTRYPOINT ["docker-entrypoint.sh"]

EXPOSE 5432
CMD ["postgres"]
