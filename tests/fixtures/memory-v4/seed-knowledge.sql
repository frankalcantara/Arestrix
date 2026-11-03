-- Fixture memory-v4: conhecimento minimo e deterministico.
-- Cobre sobreposicao funcional, edge_hash duplicado, predicado desconhecido,
-- encerramentos por claim_conflict e single_valued_predicate, fontes com duas
-- versoes, trechos, faceta, playbook, licao consolidada e evidencia orfa.

INSERT INTO memory_scope(id, kind, parent_id, name, created_at, metadata)
VALUES('project:memory-v4', 'project', NULL, 'memory-v4', '2026-01-01T00:00:00Z', '{}');

INSERT INTO memory_meta(key, value) VALUES('schema_version', '4');
INSERT INTO memory_meta(key, value) VALUES('knowledge_version', '1');

INSERT INTO memory_node(id, scope_id, kind, canonical_name, aliases_json, created_at, updated_at, metadata) VALUES
('e_lia',    'project:memory-v4', 'entity', 'Lia',    '[]', '2026-01-01T00:00:00Z', '2026-01-01T00:00:00Z', '{}'),
('e_sugar',  'project:memory-v4', 'entity', 'sugar',  '[]', '2026-01-01T00:00:00Z', '2026-01-01T00:00:00Z', '{}'),
('e_honey',  'project:memory-v4', 'entity', 'honey',  '[]', '2026-01-01T00:00:00Z', '2026-01-01T00:00:00Z', '{}'),
('e_tea',    'project:memory-v4', 'entity', 'tea',    '[]', '2026-01-01T00:00:00Z', '2026-01-01T00:00:00Z', '{}'),
('e_mint',   'project:memory-v4', 'entity', 'mint tea', '[]', '2026-01-01T00:00:00Z', '2026-01-01T00:00:00Z', '{}'),
('e_caffeine','project:memory-v4', 'entity', 'caffeine', '[]', '2026-01-01T00:00:00Z', '2026-01-01T00:00:00Z', '{}'),
('d_long',   'project:memory-v4', 'domain', 'long',   '[]', '2026-01-01T00:00:00Z', '2026-01-01T00:00:00Z', '{}'),
('r_orion',  'project:memory-v4', 'rule',   'O codigo do projeto e ORION-42.', '[]', '2026-01-01T00:00:00Z', '2026-01-01T00:00:00Z', '{}');

-- Sobreposicao funcional: dois valores ativos para Lia/PREFERS.
INSERT INTO memory_edge(id, scope_id, src_id, dst_id, relation, value_json, valid_from, valid_to,
  recorded_at, confidence, status, superseded_by, source_kind, source_ref, evidence_hash, edge_hash,
  metadata, last_used, success_count, failure_count, utility, dormant_at) VALUES
('e_overlap_a', 'project:memory-v4', 'e_lia', 'e_sugar', 'PREFERS', NULL,
 '2026-01-01T00:00:00Z', NULL, '2026-01-01T00:00:00Z', 0.8, 'active', NULL,
 'fixture', 'seed', NULL, 'overlap_hash_a', '{"claim":true,"knownPredicate":true}', NULL, 0, 0, 0.5, NULL),
('e_overlap_b', 'project:memory-v4', 'e_lia', 'e_honey', 'PREFERS', NULL,
 '2026-01-01T00:00:00Z', NULL, '2026-01-01T00:00:00Z', 0.8, 'active', NULL,
 'fixture', 'seed', NULL, 'overlap_hash_b', '{"claim":true,"knownPredicate":true}', NULL, 0, 0, 0.5, NULL);

-- Duplicata exata de edge_hash ativo.
INSERT INTO memory_edge(id, scope_id, src_id, dst_id, relation, value_json, valid_from, valid_to,
  recorded_at, confidence, status, superseded_by, source_kind, source_ref, evidence_hash, edge_hash,
  metadata, last_used, success_count, failure_count, utility, dormant_at) VALUES
('e_dup_1', 'project:memory-v4', 'e_lia', 'e_tea', 'MENTIONS', NULL,
 '2026-01-02T00:00:00Z', NULL, '2026-01-02T00:00:00Z', 0.7, 'active', NULL,
 'fixture', 'seed', NULL, 'duplicate_hash', '{"claim":true,"knownPredicate":true}', NULL, 0, 0, 0.5, NULL),
('e_dup_2', 'project:memory-v4', 'e_lia', 'e_tea', 'MENTIONS', NULL,
 '2026-01-02T00:00:00Z', NULL, '2026-01-02T00:00:00Z', 0.7, 'active', NULL,
 'fixture', 'seed', NULL, 'duplicate_hash', '{"claim":true,"knownPredicate":true}', NULL, 0, 0, 0.5, NULL);

-- Encerramentos plantados: MENTIONS com claim_conflict e PART_OF com single_valued_predicate.
INSERT INTO memory_edge(id, scope_id, src_id, dst_id, relation, value_json, valid_from, valid_to,
  recorded_at, confidence, status, superseded_by, source_kind, source_ref, evidence_hash, edge_hash,
  metadata, last_used, success_count, failure_count, utility, dormant_at) VALUES
('e_mentions_old', 'project:memory-v4', 'e_lia', 'e_mint', 'MENTIONS', NULL,
 '2026-01-03T00:00:00Z', '2026-02-01T00:00:00Z', '2026-01-03T00:00:00Z', 0.7, 'superseded', 'e_mentions_new',
 'fixture', 'seed', NULL, 'mentions_old_hash',
 '{"claim":true,"knownPredicate":true,"invalidationReason":"claim_conflict"}', NULL, 0, 0, 0.5, NULL),
('e_mentions_new', 'project:memory-v4', 'e_lia', 'e_tea', 'MENTIONS', NULL,
 '2026-02-01T00:00:00Z', NULL, '2026-02-01T00:00:00Z', 0.7, 'active', NULL,
 'fixture', 'seed', NULL, 'mentions_new_hash', '{"claim":true,"knownPredicate":true}', NULL, 0, 0, 0.5, NULL),
('e_partof_old', 'project:memory-v4', 'e_mint', 'e_tea', 'PART_OF', NULL,
 '2026-01-03T00:00:00Z', '2026-02-01T00:00:00Z', '2026-01-03T00:00:00Z', 0.7, 'superseded', 'e_partof_new',
 'fixture', 'seed', NULL, 'partof_old_hash',
 '{"claim":true,"knownPredicate":true,"invalidationReason":"single_valued_predicate"}', NULL, 0, 0, 0.5, NULL),
('e_partof_new', 'project:memory-v4', 'e_mint', 'e_caffeine', 'PART_OF', NULL,
 '2026-02-01T00:00:00Z', NULL, '2026-02-01T00:00:00Z', 0.7, 'active', NULL,
 'fixture', 'seed', NULL, 'partof_new_hash', '{"claim":true,"knownPredicate":true}', NULL, 0, 0, 0.5, NULL);

-- Predicado desconhecido, para a fracao de knownPredicate=false.
INSERT INTO memory_edge(id, scope_id, src_id, dst_id, relation, value_json, valid_from, valid_to,
  recorded_at, confidence, status, superseded_by, source_kind, source_ref, evidence_hash, edge_hash,
  metadata, last_used, success_count, failure_count, utility, dormant_at) VALUES
('e_unknown', 'project:memory-v4', 'e_lia', 'e_tea', 'INVENTED_RELATION', NULL,
 '2026-01-04T00:00:00Z', NULL, '2026-01-04T00:00:00Z', 0.6, 'active', NULL,
 'fixture', 'seed', NULL, 'unknown_hash', '{"claim":true,"knownPredicate":false}', NULL, 0, 0, 0.5, NULL);

-- Licao consolidada estrutural.
INSERT INTO memory_edge(id, scope_id, src_id, dst_id, relation, value_json, valid_from, valid_to,
  recorded_at, confidence, status, superseded_by, source_kind, source_ref, evidence_hash, edge_hash,
  metadata, last_used, success_count, failure_count, utility, dormant_at) VALUES
('e_lesson', 'project:memory-v4', 'd_long', 'r_orion', 'HAS_LESSON', NULL,
 '2026-01-01T00:00:00Z', NULL, '2026-01-01T00:00:00Z', 0.99, 'active', NULL,
 'promotion', 'seed', NULL, 'lesson_hash',
 '{"domain":"long","lessonRule":"O codigo do projeto e ORION-42.","claim":true,"knownPredicate":true}', NULL, 0, 0, 0.5, NULL);

-- Fontes: F86, F90 e F92 com duas versoes.
INSERT INTO memory_source(id, scope_id, uri, content_hash, kind, size, first_seen, last_seen, status) VALUES
('src_f86', 'project:memory-v4', 'file://F86.md', 'hash_f86_v1', 'text', 240, '2026-01-01T00:00:00Z', '2026-01-01T00:00:00Z', 'active'),
('src_f90', 'project:memory-v4', 'file://F90.md', 'hash_f90_v1', 'text', 220, '2026-01-01T00:00:00Z', '2026-01-01T00:00:00Z', 'active'),
('src_f92_v1', 'project:memory-v4', 'file://F92.md', 'hash_f92_v1', 'text', 210, '2026-01-01T00:00:00Z', '2026-01-01T00:00:00Z', 'active'),
('src_f92_v2', 'project:memory-v4', 'file://F92.md', 'hash_f92_v2', 'text', 212, '2026-02-01T00:00:00Z', '2026-02-01T00:00:00Z', 'active');

INSERT INTO memory_passage(id, source_id, scope_id, ordinal, start, end, text, text_hash, tokens, status, created_at) VALUES
('psg_f86_1', 'src_f86', 'project:memory-v4', 0, 0, 120,
 'F86: Lia evitou cafeina a noite; este trecho tem exatamente cento e vinte bytes para o teste de copia literal.', 'ph_f86_1', 20, 'active', '2026-01-01T00:00:00Z'),
('psg_f90_1', 'src_f90', 'project:memory-v4', 0, 0, 80,
 'F90: o cha de hortela nao contem cafeina.', 'ph_f90_1', 12, 'active', '2026-01-01T00:00:00Z'),
('psg_f92_1', 'src_f92_v1', 'project:memory-v4', 0, 0, 80,
 'F92 v1: Lia aceitou cha de hortela.', 'ph_f92_1', 10, 'active', '2026-01-01T00:00:00Z'),
('psg_f92_2', 'src_f92_v2', 'project:memory-v4', 0, 0, 82,
 'F92 v2: Lia aceitou cha de hortela novamente.', 'ph_f92_2', 10, 'active', '2026-02-01T00:00:00Z');

-- Afirmacoes ligadas as fontes.
INSERT INTO memory_edge(id, scope_id, src_id, dst_id, relation, value_json, valid_from, valid_to,
  recorded_at, confidence, status, superseded_by, source_kind, source_ref, evidence_hash, edge_hash,
  metadata, last_used, success_count, failure_count, utility, dormant_at) VALUES
('e_f86_claim', 'project:memory-v4', 'e_lia', 'e_caffeine', 'MENTIONS', '{"value":"evita"}',
 '2026-01-01T00:00:00Z', NULL, '2026-01-01T00:00:00Z', 0.8, 'active', NULL,
 'claim', 'src_f86', NULL, 'f86_claim_hash',
 '{"claim":true,"knownPredicate":true,"passageId":"psg_f86_1","sourceUri":"file://F86.md"}', NULL, 0, 0, 0.5, NULL),
('e_f90_claim', 'project:memory-v4', 'e_mint', 'e_caffeine', 'MENTIONS', '{"value":"nao contem"}',
 '2026-01-01T00:00:00Z', NULL, '2026-01-01T00:00:00Z', 0.8, 'active', NULL,
 'claim', 'src_f90', NULL, 'f90_claim_hash',
 '{"claim":true,"knownPredicate":true,"passageId":"psg_f90_1","sourceUri":"file://F90.md"}', NULL, 0, 0, 0.5, NULL),
('e_f92v1_claim', 'project:memory-v4', 'e_lia', 'e_mint', 'MENTIONS', '{"value":"aceitou"}',
 '2026-01-01T00:00:00Z', NULL, '2026-01-01T00:00:00Z', 0.8, 'active', NULL,
 'claim', 'src_f92_v1', NULL, 'f92v1_claim_hash',
 '{"claim":true,"knownPredicate":true,"passageId":"psg_f92_1","sourceUri":"file://F92.md"}', NULL, 0, 0, 0.5, NULL),
('e_f92v2_claim', 'project:memory-v4', 'e_lia', 'e_mint', 'MENTIONS', '{"value":"aceitou novamente"}',
 '2026-02-01T00:00:00Z', NULL, '2026-02-01T00:00:00Z', 0.8, 'active', NULL,
 'claim', 'src_f92_v2', NULL, 'f92v2_claim_hash',
 '{"claim":true,"knownPredicate":true,"passageId":"psg_f92_2","sourceUri":"file://F92.md"}', NULL, 0, 0, 0.5, NULL);

-- Lente e faceta derivada das tres fontes.
INSERT INTO memory_lens(id, scope_id, name, description, version, created_at, updated_at) VALUES
('lens_topic', 'project:memory-v4', 'topic', 'Topico', 1, '2026-01-01T00:00:00Z', '2026-01-01T00:00:00Z');

INSERT INTO memory_facet(id, scope_id, entity_id, lens_id, revision, description, supporting_claims_json,
  claims_hash, status, embedding_fingerprint, created_at, updated_at) VALUES
('facet_lia', 'project:memory-v4', 'e_lia', 'lens_topic', 1,
 'Lia evita cafeina e aceita cha de hortela.',
 '["e_f86_claim","e_f90_claim","e_f92v1_claim","e_f92v2_claim"]',
 'facet_claims_hash', 'active', '', '2026-01-01T00:00:00Z', '2026-01-01T00:00:00Z');

-- Playbook derivado da fonte F86.
INSERT INTO memory_playbook(id, scope_id, task_type, title, steps_json, preconditions_json,
  success_count, failure_count, confidence, status, source_ref, evidence_json, created_at, updated_at) VALUES
('pb_long', 'project:memory-v4', 'long', 'Diagnostico longo',
 '[{"tool":"read","action":"ler fonte"},{"tool":"write","action":"registrar"}]', '{}',
 0, 0, 0.8, 'candidate', 'src_f86', '["e_f86_claim"]',
 '2026-01-01T00:00:00Z', '2026-01-01T00:00:00Z');

-- Evidencias: uma resolvivel e uma orfa plantada.
INSERT INTO memory_evidence(id, scope_id, target_kind, target_id, action, episode_id, run_id, node_id,
  input_hash, validator_id, validator_version, ok, artifact_hash, excerpt_hash, summary, created_at) VALUES
('ev_f86_claim', 'project:memory-v4', 'claim', 'e_f86_claim', 'create', 'ep_T1', 'runT1', 'node1',
 'input1', 'validator', '1', 1, NULL, 'excerpt_f86', 'evidencia F86', '2026-01-01T00:00:00Z'),
('ev_orphan', 'project:memory-v4', 'claim', 'e_missing_claim', 'create', 'ep_T1', 'runT1', 'node1',
 'input1', 'validator', '1', 1, NULL, 'excerpt_orphan', 'evidencia orfa', '2026-01-01T00:00:00Z');

INSERT INTO memory_claim_extraction(id, scope_id, passage_id, passage_hash, extractor_fingerprint,
  status, error, created_at) VALUES
('ce_f86', 'project:memory-v4', 'psg_f86_1', 'ph_f86_1', 'fixture-extractor', 'proposed', '', '2026-01-01T00:00:00Z');
