-- Fixture memory-v4: telemetria minima (T1, T2, T3, T4b do MG15).
INSERT INTO memory_scope(id, kind, parent_id, name, created_at, metadata)
VALUES('project:memory-v4', 'project', NULL, 'memory-v4', '2026-01-01T00:00:00Z', '{}');
INSERT INTO memory_meta(key, value) VALUES('schema_version', '4');
INSERT INTO memory_meta(key, value) VALUES('knowledge_version', '1');

INSERT INTO memory_episode(id, scope_id, run_id, node_id, item_id, kind, content, sanitized, ab_arm,
  ab_repetition, created_at, metadata, intent_template_hash, task_type, outcome, error_class,
  input_hash, tool_calls_json, files_touched_json, artifacts_json, evidence_json, success_signals_json,
  redactions) VALUES
('ep_T1', 'project:memory-v4', 'runT1', 'node1', 'pipeline', 'agent_success',
 'T1 aprovado', 1, '', 0, '2026-01-01T00:00:00Z', '{}', 'intent1', 'evening_drink', 'success', '',
 'input1', '[]', '[]', '[]', '[]', '{}', 0),
('ep_T2', 'project:memory-v4', 'runT2', 'node1', 'pipeline', 'agent_success',
 'T2 aprovado', 1, '', 0, '2026-01-02T00:00:00Z', '{}', 'intent1', 'evening_drink', 'success', '',
 'input2', '[]', '[]', '[]', '[]', '{}', 0),
('ep_T3', 'project:memory-v4', 'runT3', 'node1', 'pipeline', 'agent_failure',
 'T3 timeout do provedor', 1, '', 0, '2026-01-03T00:00:00Z', '{}', 'intent1', 'evening_drink', 'failure',
 'provider_timeout', 'input3', '[]', '[]', '[]', '[]', '{}', 0),
('ep_T4b', 'project:memory-v4', 'runT4b', 'node1', 'pipeline', 'agent_failure',
 'T4b cafe recomendado a noite', 1, '', 0, '2026-01-04T00:00:00Z', '{}', 'intent1', 'evening_drink',
 'failure', 'content_failure', 'input4', '[]', '[]', '[]', '[]', '{}', 0),
('ep_copy', 'project:memory-v4', 'runCopy', 'node1', 'pipeline', 'agent_success',
 'F86: Lia evitou cafeina a noite; este trecho tem exatamente cento e vinte bytes para o teste de copia literal.',
 1, '', 0, '2026-01-05T00:00:00Z',
 '{"passageId":"psg_f86_1","sourceUri":"file://F86.md"}', 'intent1', 'evening_drink', 'success', '',
 'input5', '[]', '[]', '[]', '[]', '{}', 0);

INSERT INTO memory_hit(id, scope_id, run_id, node_id, memory_id, kind, injected, consulted, used,
  outcome, ab_arm, ab_repetition, created_at) VALUES
('h_t1_c52', 'project:memory-v4', 'runT1', 'node1', 'e_c52', 'fact', 1, 0, 1, 'success', '', 0, '2026-01-01T00:00:00Z'),
('h_t1_c71', 'project:memory-v4', 'runT1', 'node1', 'e_c71', 'fact', 1, 0, 1, 'success', '', 0, '2026-01-01T00:00:00Z'),
('h_t1_c04', 'project:memory-v4', 'runT1', 'node1', 'e_c04', 'fact', 1, 0, 0, 'success', '', 0, '2026-01-01T00:00:00Z'),
('h_t2_c71', 'project:memory-v4', 'runT2', 'node1', 'e_c71', 'fact', 1, 0, 1, 'success', '', 0, '2026-01-02T00:00:00Z'),
('h_t2_c04', 'project:memory-v4', 'runT2', 'node1', 'e_c04', 'fact', 1, 0, 1, 'success', '', 0, '2026-01-02T00:00:00Z'),
('h_t3_c52', 'project:memory-v4', 'runT3', 'node1', 'e_c52', 'fact', 1, 0, 0, 'unknown', '', 0, '2026-01-03T00:00:00Z'),
('h_t4b_c52', 'project:memory-v4', 'runT4b', 'node1', 'e_c52', 'fact', 1, 0, 0, 'failure', '', 0, '2026-01-04T00:00:00Z'),
('h_t4b_c71', 'project:memory-v4', 'runT4b', 'node1', 'e_c71', 'fact', 1, 0, 1, 'failure', '', 0, '2026-01-04T00:00:00Z'),
('h_t4b_c86', 'project:memory-v4', 'runT4b', 'node1', 'e_c86', 'fact', 1, 0, 0, 'failure', '', 0, '2026-01-04T00:00:00Z'),
('h_t4c_c86', 'project:memory-v4', 'runT4c', 'node1', 'e_c86', 'fact', 1, 0, 0, 'failure', '', 0, '2026-01-05T00:00:00Z');

INSERT INTO memory_validation(id, scope_id, run_id, node_id, episode_id, validator_id,
  validator_version, ok, score, reasons_json, artifact_hash, ab_arm, ab_repetition, created_at) VALUES
('v_T1', 'project:memory-v4', 'runT1', 'node1', 'ep_T1', 'fixture-validator', '1', 1, 1.0, '[]', NULL, '', 0, '2026-01-01T00:00:00Z'),
('v_T2', 'project:memory-v4', 'runT2', 'node1', 'ep_T2', 'fixture-validator', '1', 1, 1.0, '[]', NULL, '', 0, '2026-01-02T00:00:00Z'),
('v_T3', 'project:memory-v4', 'runT3', 'node1', 'ep_T3', 'fixture-validator', '1', 0, 0.0, '["infra"]', NULL, '', 0, '2026-01-03T00:00:00Z'),
('v_T4b', 'project:memory-v4', 'runT4b', 'node1', 'ep_T4b', 'fixture-validator', '1', 0, 0.0, '["content"]', NULL, '', 0, '2026-01-04T00:00:00Z');

INSERT INTO memory_usage(id, scope_id, run_id, node_id, provider, model, prompt_tokens,
  completion_tokens, total_tokens, cached_tokens, cache_write_tokens, calls, latency_ms,
  memory_context_chars, memory_enabled, retries, invalid_citations, outcome, ab_arm, ab_repetition, created_at) VALUES
('u_T1', 'project:memory-v4', 'runT1', 'node1', 'fixture', 'fixture', 1000, 100, 1100, 0, 0, 2, 10, 200, 1, 0, 0, 'success', '', 0, '2026-01-01T00:00:00Z'),
('u_T2', 'project:memory-v4', 'runT2', 'node1', 'fixture', 'fixture', 900, 90, 990, 0, 0, 2, 10, 200, 1, 0, 0, 'success', '', 0, '2026-01-02T00:00:00Z'),
('u_T3', 'project:memory-v4', 'runT3', 'node1', 'fixture', 'fixture', 800, 0, 800, 0, 0, 1, 10, 200, 1, 0, 0, 'failure', '', 0, '2026-01-03T00:00:00Z'),
('u_T4b', 'project:memory-v4', 'runT4b', 'node1', 'fixture', 'fixture', 950, 80, 1030, 0, 0, 2, 10, 200, 1, 0, 0, 'failure', '', 0, '2026-01-04T00:00:00Z');

INSERT INTO memory_trace(trace_id, scope_id, run_id, node_id, item_id, task_type,
  intent_template_hash, input_hash, knowledge_version, model, tier, outcome, anchors_json,
  steps_json, delivered_json, used_json, truncated_steps, created_at, updated_at) VALUES
('tr_T1', 'project:memory-v4', 'runT1', 'node1', 'pipeline', 'evening_drink', 'intent1',
 'input1', 1, 'fixture', 'default', 'success', '[]', '[]',
 '["e_c52","e_c71","e_c04"]', '["e_c52","e_c71"]', 0, '2026-01-01T00:00:00Z', '2026-01-01T00:00:00Z'),
('tr_T4b', 'project:memory-v4', 'runT4b', 'node1', 'pipeline', 'evening_drink', 'intent1',
 'input4', 1, 'fixture', 'default', 'failure', '[]', '[]',
 '["e_c52","e_c71","e_c86"]', '["e_c71"]', 0, '2026-01-04T00:00:00Z', '2026-01-04T00:00:00Z');
