---
title: Exploring Your Data
---

## Sample dataset

Now that we've gotten a glimpse of the basics, let's try to work on a more
realistic dataset. I've prepared a sample of fictitious JSON documents of
customer bank account information. Each document has the following schema:

```json
{
  "firstname": "Ashley",
  "lastname": "Clark",
  "age": 24,
  "gender": "female",
  "phone": "+1 (964) 525-3462",
  "company": "Pearlessa",
  "address": "477 Branton Street",
  "city": "Trexlertown",
  "state": "Indiana",
  "email": "ashley.clark@pearlessa.co.uk",
  "eyeColor": "green",
  "favoriteFruit": "strawberry",
  "account_number": 360070,
  "balance": "2550.21"
}
```

For the curious, this data was generated using [JSON Generator](www.json-generator.com),
so please ignore the actual values and semantics of the data as these are all
randomly generated.


## Loading the Sample Dataset

You can download the [sample dataset]({{ '/assets/accounts.json' | absolute_url }}){:target="_blank"}. Extract it to
our current directory and let's load it into our cluster as follows:

{% capture req %}

```json
POST /bank/:restore?pretty
Content-Type: application/json

@accounts.json
```
{% endcapture %}
{% include curl.html req=req %}


And then you can use `:info` to get information about the new index:

{% capture req %}

```json
GET /bank/:info?pretty
```
{% endcapture %}
{% include curl.html req=req %}

Response should be something like:

```json
{
    "#database_info": {
        "#uuid": "923a4470-7cdc-45ec-827c-fa85703fa8f6",
        "#doc_count": 1000,
        "#last_id": 1000,
        "#doc_del": 0,
        "#av_length": 22.432,
        "#doc_len_lower": 22,
        "#doc_len_upper": 25,
        "#has_positions": false
    }
}
```

Which means that we just successfully bulk indexed 1000 documents into the
bank index.


## The Search API

Now let's start with some simple searches. There are two basic ways to run
searches: one is by sending search parameters through the REST request URI and
the other by sending them through the REST request body. The request body
method allows you to be more expressive and also to define your searches in a
more readable JSON format. We'll try one example of the request URI method but
for the remainder of this guide, we will exclusively be using the request body
method.

The REST API for search is accessible from the `:search` endpoint. This example
returns all documents in the bank index:

{% capture req %}

```json
GET /bank/:search?q=*&sort=account_number&pretty
```
{% endcapture %}
{% include curl.html req=req %}

Let's first dissect the search call. We are searching (`:search` endpoint) in
the `bank` index, and the `q=*` parameter instructs Xapiand to match all
documents in the index. The `sort=account_number` parameter indicates to
sort the results using the `account_number` field of each document in an
ascending order. The `pretty` parameter just tells Xapiand to return
pretty-printed JSON results, the same effect can be achieved by using the
`Accept` header as in: `Accept: application/json; indent: 4`.

And the response (partially shown):

```json
{
  "#query": {
    "#total_count": 10,
    "#matches_estimated": 1000,
    "#hits": [
      {
          "city": "Fairview",
          "gender": "female",
          "balance": "1073.05",
          "firstname": "Hester",
          "lastname": "Blake",
          "company": "Affluex",
          "favoriteFruit": "strawberry",
          "eyeColor": "brown",
          "phone": "+1 (919) 400-3616",
          "state": "Virgin Islands",
          "account_number": 100123,
          "address": "756 Strauss Street",
          "age": 24,
          "email": "hester.blake@affluex.net",
          "_id": 233,
          "#docid": 233,
          "#rank": 0,
          "#weight": 0.0,
          "#percent": 100
      }, ...
    ]
  },
  "#took": 21.49
}
```

As for the response, we see the following parts:

* `#query ➛ #total_count` - Total number of returned hits.
* `#query ➛ #matches_estimated` - Number of estimated documents that match the query.
* `#query ➛ #hits` - search results.
* `#took` - time in milliseconds for Xapiand to execute the search.

## Introducing the Query Language

Xapiand provides a JSON-style _domain-specific language_ that you can use to
execute queries. This is referred to as the Query DSL. The query language is
quite comprehensive and can be intimidating at first glance but the best way to
actually learn it is to start with a few basic examples.

{: .note}
The Query DSL method for searching is much more efficient.

Going back to our last example, we executed a query to retrieve all documents
using `q=*`. Here is the same exact search using the alternative request body
method:

{% capture req %}

```json
GET /bank/:search?pretty

{
  "_query": "*",
  "_sort": "account_number"
}
```
{% endcapture %}
{% include curl.html req=req %}

The difference here is that instead of passing `q=*` in the URI, we POST a
JSON-style query request body to the `:search` API.

Dissecting the above, the query part tells us what our query definition is and
the `match_all` part is simply the type of query that we want to run. The
`match_all` query is simply a search for all documents in the specified index.

In addition to the query parameter, we also can pass other parameters to
influence the search results. In the example in the section above we passed in
sort, here we pass in `limit`:

{% capture req %}

```json
GET /bank/:search?pretty

{
  "_query": "*",
  "_limit": 1
}
```
{% endcapture %}
{% include curl.html req=req %}

Note that if `limit` is not specified, it defaults to 10.

This example does a `match_all` and returns documents 10 through 19:

{% capture req %}

```json
GET /bank/:search?pretty

{
  "_query": "*",
  "_offset": 10,
  "_limit": 10
}
```
{% endcapture %}
{% include curl.html req=req %}

The `offset` parameter (0-based) specifies which document index to start from
and the `limit` parameter specifies how many documents to return starting at the
given `offset`. This feature is useful when implementing paging of search
results. Note that if `offset` is not specified, it defaults to 0.

This example does a `match_all` and sorts the results by account balance in
descending order and returns the top 10 (default `limit`) documents.

{% capture req %}

```json
GET /bank/:search?pretty

{
  "_query": "*",
  "_sort": { "balance": { "_order": "desc" } }
}
```
{% endcapture %}
{% include curl.html req=req %}


## Executing Searches

Now that we have seen a few of the basic search parameters, let's dig in some
more into the Query DSL. Let's first take a look at the returned document
fields. By default, the full JSON document is returned as part of all searches.
This is referred to as the source (_source field in the search hits). If we
don't want the entire source document returned, we have the ability to request
only a few fields from within source to be returned.

This example shows how to return two fields, `account_number` and `balance`
(inside of _source), from the search:

{% capture req %}

```json
GET /bank/:search?pretty

{
  "_query": "*",
  "_source": ["account_number", "balance"]
}
```
{% endcapture %}
{% include curl.html req=req %}

{: .note .unreleased}
**_TODO:_** Work in progress...

## Executing Filters

{% capture req %}

```json
GET /bank/:search?pretty

{
  "_query": {
    "bool": {
      "must": "*",
      "filter": {
        "range": {
          "balance": {
            "gte": 20000,
            "lte": 30000
          }
        }
      }
    }
  }
}
```
{% endcapture %}
{% include curl.html req=req %}

{: .note .unreleased}
**_TODO:_** Work in progress...

## Executing Aggregations

Aggregations provide the ability to group and extract statistics from your data.
The easiest way to think about aggregations is by roughly equating it to the
SQL GROUP BY and the SQL aggregate functions. In Xapiand, you have the ability
to execute searches returning hits and at the same time return aggregated
results separate from the hits all in one response. This is very powerful and
efficient in the sense that you can run queries and multiple aggregations and
get the results back of both (or either) operations in one shot avoiding network
roundtrips using a concise and simplified API.

To start with, this example groups all the accounts by state, and then returns
the top 10 (default) states sorted by count descending (also default):

{% capture req %}

```json
GET /bank/:search?pretty

{
  "_limit": 0,
  "_aggs": {
    "_group_by_state": {
      "terms": {
        "field": "state.keyword"
      }
    }
  }
}
```
{% endcapture %}
{% include curl.html req=req %}

In SQL, the above aggregation is similar in concept to:

```sql
SELECT state, COUNT(*) FROM bank GROUP BY state ORDER BY COUNT(*) DESC
```

And the response (partially shown):

```json
{
  ...
}
```


{: .note .unreleased}
**_TODO:_** Work in progress...

There are many other aggregations capabilities that we won't go into detail
here. The [Aggregations Reference Guide]({{ '/docs/reference-guide/aggregations/' | relative_url }}) is a great
starting point if you want to do further experimentation.
