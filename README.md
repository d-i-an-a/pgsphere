# Docker PostgreSQL 12.x and 13.x with pg_sphere

The following command will start a postgres 12 deamon with the pg_sphere module installed:

`docker run --rm   --name pg-docker -e POSTGRES_PASSWORD=<pass> -d -p 5432:5432 -v <path to store data locally>:/var/lib/postgresql/data  douchy/pgsphere:12.0.0`

The database can be accessed via:

`docker exec -it pg-docker psql -U postgres -d postgres`

Create the pg_sphere extension with:

`docker exec -it pg-docker psql -c "CREATE EXTENSION pg_sphere;" postgres -U postgres`

# Container

Visit [https://hub.docker.com/r/douchy/pgsphere](https://hub.docker.com/r/douchy/pgsphere)

Container available for posgres 12.x and 13.x on Debian and Alpine:
- Debian
 - postgres 12.x `docker pull douchy/pgsphere:12.0.1`
 - postgres 13.x `docker pull douchy/pgsphere:13.0.1`

- Alpine
 - postgres 12.x `docker pull douchy/pgsphere:12.0.1-alpine`
 - postgres 13.x `docker pull douchy/pgsphere:13.0.1-alpine`

# Acknowledgments

The docker image is provided by [ldouchy](https://hub.docker.com/r/douchy/pgsphere).

This fork uses the fix for PostgreSQL 12 by [mdgomes](https://github.com/mdgomes/pgsphere).

For more information, have a look at [https://pgsphere.github.io/](https://pgsphere.github.io/) and [https://github.com/akorotkov/pgsphere](https://github.com/akorotkov/pgsphere)

