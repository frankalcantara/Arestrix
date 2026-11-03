# Regras do dump canônico do fixture memory-v4

O fixture é montado em tempo de teste a partir de `schema.sql`, `seed-knowledge.sql`
e `seed-telemetry.sql`. Nenhum arquivo `.db` é versionado.

`scripts/memory_fixture_check.py` cria dois bancos temporários, aplica os SQL e
produz um dump canônico com as regras abaixo. O SHA-256 desse dump é comparado
com `fixture.sha256`.

1. As tabelas aparecem em ordem alfabética.
2. Dentro de cada tabela, as linhas são ordenadas pela chave primária. Quando não
   há chave primária, a ordenação usa todas as colunas na ordem do `PRAGMA
   table_info`, com `NULL` antes dos demais valores.
3. Tabelas-sombra do FTS5 (`*_data`, `*_idx`, `*_docsize`, `*_config`,
   `*_content`) não entram no dump. A tabela virtual é despejada com
   `SELECT rowid, * FROM <tabela> ORDER BY rowid`.
4. `sqlite_sequence` entra quando existir, porque as fases seguintes usam
   sequências que não podem recuar.
5. Cada linha é serializada como JSON com chaves ordenadas e UTF-8 preservado.
   BLOBs são representados por `hex:<sha256 do blob>`.
6. O dump inclui o nome da tabela, as colunas e as linhas nessa ordem. Duas
   montagens do fixture precisam produzir o mesmo SHA-256.

Para atualizar o hash depois de uma mudança intencional no seed:

```powershell
py -3 scripts/memory_fixture_check.py --write
```
