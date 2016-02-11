#include "collation.h"

/*
 * CREATE COLLATION
 * ALTER COLLATION
 * DROP COLLATION
 * COMMENT ON COLLATION
 *
 * TODO
 *
 * ALTER COLLATION ... RENAME TO
 * ALTER COLLATION ... SET SCHEMA
 */

PQLCollation *
getCollations(PGconn *c, int *n)
{
	char			*query = NULL;
	int				nquery = PGQQRYLEN;
	PQLCollation	*d;
	PGresult		*res;
	int				i;
	int				r;

	logNoise("collation: server version: %d", PQserverVersion(c));

	do
	{
		query = (char *) malloc(nquery * sizeof(char));

		r = snprintf(query, nquery,
					 "SELECT c.oid, n.nspname, collname, pg_encoding_to_char(collencoding) AS collencoding, collcollate, collctype, pg_get_userbyid(collowner) AS collowner, obj_description(c.oid, 'pg_conversion') AS description FROM pg_collation c INNER JOIN pg_namespace n ON (c.collnamespace = n.oid) LEFT JOIN (pg_description d INNER JOIN pg_class x ON (x.oid = d.classoid AND x.relname = 'pg_conversion')) ON (d.objoid = c.oid) WHERE c.oid >= %u ORDER BY n.nspname, collname",
					 PGQ_FIRST_USER_OID);

		if (r < nquery)
			break;

		logNoise("query size: required (%u) ; initial (%u)", r, nquery);
		nquery = r + 1;	/* make enough room for query */
		free(query);
	}
	while (true);

	res = PQexec(c, query);

	free(query);

	if (PQresultStatus(res) != PGRES_TUPLES_OK)
	{
		logError("query failed: %s", PQresultErrorMessage(res));
		PQclear(res);
		PQfinish(c);
		/* XXX leak another connection? */
		exit(EXIT_FAILURE);
	}

	*n = PQntuples(res);
	if (*n > 0)
		d = (PQLCollation *) malloc(*n * sizeof(PQLCollation));
	else
		d = NULL;

	logDebug("number of collations in server: %d", *n);

	for (i = 0; i < *n; i++)
	{
		d[i].obj.oid = strtoul(PQgetvalue(res, i, PQfnumber(res, "oid")), NULL, 10);
		d[i].obj.schemaname = strdup(PQgetvalue(res, i, PQfnumber(res, "nspname")));
		d[i].obj.objectname = strdup(PQgetvalue(res, i, PQfnumber(res, "collname")));
		d[i].encoding = strdup(PQgetvalue(res, i, PQfnumber(res, "collencoding")));
		d[i].collate = strdup(PQgetvalue(res, i, PQfnumber(res, "collcollate")));
		d[i].ctype = strdup(PQgetvalue(res, i, PQfnumber(res, "collctype")));

		if (PQgetisnull(res, i, PQfnumber(res, "description")))
			d[i].comment = NULL;
		else
			d[i].comment = strdup(PQgetvalue(res, i, PQfnumber(res, "description")));

		d[i].owner = strdup(PQgetvalue(res, i, PQfnumber(res, "collowner")));

		logDebug("collation \"%s\".\"%s\"", d[i].obj.schemaname, d[i].obj.objectname);
	}

	PQclear(res);

	return d;
}

void
freeCollations(PQLCollation *c, int n)
{
	if (n > 0)
	{
		int	i;

		for (i = 0; i < n; i++)
		{
			free(c[i].obj.schemaname);
			free(c[i].obj.objectname);
			free(c[i].encoding);
			free(c[i].collate);
			free(c[i].ctype);
			if (c[i].comment)
				free(c[i].comment);
			free(c[i].owner);
		}

		free(c);
	}
}

void
dumpCreateCollation(FILE *output, PQLCollation c)
{
	/*
	 * All pg_conversion columns are not null, specifying collate and ctype are
	 * more flexible than locale because locale implies we can't specify
	 * collate or ctype.
	 */
	fprintf(output, "\n\n");
	fprintf(output, "CREATE COLLATION %s.%s (LC_COLLATE = '%s', LC_CTYPE = '%s');",
			formatObjectIdentifier(c.obj.schemaname),
			formatObjectIdentifier(c.obj.objectname),
			c.collate,
			c.ctype);

	/* comment */
	if (options.comment && c.comment != NULL)
	{
		fprintf(output, "\n\n");
		fprintf(output, "COMMENT ON COLLATION %s.%s IS '%s';",
				formatObjectIdentifier(c.obj.schemaname),
				formatObjectIdentifier(c.obj.objectname),
				c.comment);
	}

	/* owner */
	if (options.owner)
	{
		fprintf(output, "\n\n");
		fprintf(output, "ALTER COLLATION %s.%s OWNER TO %s;",
				formatObjectIdentifier(c.obj.schemaname),
				formatObjectIdentifier(c.obj.objectname),
				c.owner);
	}
}

void
dumpDropCollation(FILE *output, PQLCollation c)
{
	fprintf(output, "\n\n");
	fprintf(output, "DROP COLLATION %s.%s;",
			formatObjectIdentifier(c.obj.schemaname),
			formatObjectIdentifier(c.obj.objectname));
}

void
dumpAlterCollation(FILE *output, PQLCollation a, PQLCollation b)
{
	/* comment */
	if (options.comment)
	{
		if ((a.comment == NULL && b.comment != NULL) ||
				(a.comment != NULL && b.comment != NULL &&
				 strcmp(a.comment, b.comment) != 0))
		{
			fprintf(output, "\n\n");
			fprintf(output, "COMMENT ON COLLATION %s.%s IS '%s';",
					formatObjectIdentifier(b.obj.schemaname),
					formatObjectIdentifier(b.obj.objectname),
					b.comment);
		}
		else if (a.comment != NULL && b.comment == NULL)
		{
			fprintf(output, "\n\n");
			fprintf(output, "COMMENT ON COLLATION %s.%s IS NULL;",
					formatObjectIdentifier(b.obj.schemaname),
					formatObjectIdentifier(b.obj.objectname));
		}
	}

	/* owner */
	if (options.owner)
	{
		if (strcmp(a.owner, b.owner) != 0)
		{
			fprintf(output, "\n\n");
			fprintf(output, "ALTER COLLATION %s.%s OWNER TO %s;",
					formatObjectIdentifier(b.obj.schemaname),
					formatObjectIdentifier(b.obj.objectname),
					b.owner);
		}
	}
}
