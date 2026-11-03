CREATE TABLE memory_alias (
  scope_id TEXT NOT NULL REFERENCES memory_scope(id),
  alias TEXT NOT NULL,
  node_id TEXT NOT NULL,
  source TEXT NOT NULL DEFAULT '',
  PRIMARY KEY (scope_id, alias)
);
CREATE TABLE memory_audit (
  id TEXT PRIMARY KEY,
  actor TEXT NOT NULL,
  action TEXT NOT NULL,
  target TEXT NOT NULL,
  before TEXT,
  after TEXT,
  created_at TEXT NOT NULL
);
CREATE TABLE memory_claim_extraction (
  id TEXT PRIMARY KEY,
  scope_id TEXT NOT NULL REFERENCES memory_scope(id),
  passage_id TEXT NOT NULL,
  passage_hash TEXT NOT NULL,
  extractor_fingerprint TEXT NOT NULL,
  status TEXT NOT NULL DEFAULT 'proposed',
  error TEXT NOT NULL DEFAULT '',
  created_at TEXT NOT NULL,
  UNIQUE(scope_id, passage_hash, extractor_fingerprint)
);
CREATE TABLE memory_claim_path (
  claim_id TEXT NOT NULL,
  path_id TEXT NOT NULL REFERENCES memory_path(id),
  PRIMARY KEY (claim_id, path_id)
);
CREATE TABLE memory_edge (
  id TEXT PRIMARY KEY,
  scope_id TEXT NOT NULL REFERENCES memory_scope(id),
  src_id TEXT NOT NULL REFERENCES memory_node(id),
  dst_id TEXT REFERENCES memory_node(id),
  relation TEXT NOT NULL,
  value_json TEXT,
  valid_from TEXT,
  valid_to TEXT,
  recorded_at TEXT NOT NULL,
  confidence REAL NOT NULL DEFAULT 0.5,
  status TEXT NOT NULL CHECK (status IN ('candidate','active','rejected','superseded')),
  superseded_by TEXT REFERENCES memory_edge(id),
  source_kind TEXT NOT NULL,
  source_ref TEXT,
  evidence_hash TEXT,
  edge_hash TEXT NOT NULL,
  metadata TEXT NOT NULL DEFAULT '{}',
  last_used TEXT,
  success_count INTEGER NOT NULL DEFAULT 0,
  failure_count INTEGER NOT NULL DEFAULT 0,
  utility REAL NOT NULL DEFAULT 0.5,
  dormant_at TEXT
);
CREATE TABLE memory_edge_lens (
  edge_id TEXT NOT NULL REFERENCES memory_edge(id),
  lens_id TEXT NOT NULL REFERENCES memory_lens(id),
  PRIMARY KEY (edge_id, lens_id)
);
CREATE TABLE memory_embedding (
  scope_id TEXT NOT NULL REFERENCES memory_scope(id),
  object_type TEXT NOT NULL,
  object_id TEXT NOT NULL,
  model TEXT NOT NULL,
  dim INTEGER NOT NULL,
  vector BLOB NOT NULL,
  content_hash TEXT NOT NULL,
  domain TEXT NOT NULL DEFAULT '',
  fingerprint TEXT NOT NULL DEFAULT '',
  created_at TEXT NOT NULL,
  PRIMARY KEY (scope_id, object_type, object_id, model)
);
CREATE TABLE memory_episode (
  id TEXT PRIMARY KEY,
  scope_id TEXT NOT NULL REFERENCES memory_scope(id),
  run_id TEXT NOT NULL,
  node_id TEXT,
  item_id TEXT,
  kind TEXT NOT NULL,
  content TEXT NOT NULL,
  sanitized INTEGER NOT NULL DEFAULT 1,
  ab_arm TEXT NOT NULL DEFAULT '',
  ab_repetition INTEGER NOT NULL DEFAULT 0,
  created_at TEXT NOT NULL,
  metadata TEXT NOT NULL DEFAULT '{}',
  intent_template_hash TEXT NOT NULL DEFAULT ''
, task_type TEXT NOT NULL DEFAULT '', outcome TEXT NOT NULL DEFAULT '', error_class TEXT NOT NULL DEFAULT '', input_hash TEXT NOT NULL DEFAULT '', tool_calls_json TEXT NOT NULL DEFAULT '[]', files_touched_json TEXT NOT NULL DEFAULT '[]', artifacts_json TEXT NOT NULL DEFAULT '[]', evidence_json TEXT NOT NULL DEFAULT '[]', success_signals_json TEXT NOT NULL DEFAULT '{}', redactions INTEGER NOT NULL DEFAULT 0);
CREATE TABLE memory_evidence (
  id TEXT PRIMARY KEY,
  scope_id TEXT NOT NULL REFERENCES memory_scope(id),
  target_kind TEXT NOT NULL,
  target_id TEXT NOT NULL,
  action TEXT NOT NULL DEFAULT 'promote',
  episode_id TEXT,
  run_id TEXT,
  node_id TEXT,
  input_hash TEXT,
  validator_id TEXT,
  validator_version TEXT,
  ok INTEGER NOT NULL DEFAULT 0,
  artifact_hash TEXT,
  excerpt_hash TEXT,
  summary TEXT NOT NULL DEFAULT '',
  created_at TEXT NOT NULL
);
CREATE TABLE memory_facet (
  id TEXT PRIMARY KEY,
  scope_id TEXT NOT NULL REFERENCES memory_scope(id),
  entity_id TEXT NOT NULL REFERENCES memory_node(id),
  lens_id TEXT NOT NULL REFERENCES memory_lens(id),
  revision INTEGER NOT NULL DEFAULT 1,
  description TEXT NOT NULL,
  supporting_claims_json TEXT NOT NULL DEFAULT '[]',
  claims_hash TEXT NOT NULL,
  status TEXT NOT NULL DEFAULT 'active',
  embedding_fingerprint TEXT NOT NULL DEFAULT '',
  created_at TEXT NOT NULL,
  updated_at TEXT NOT NULL,
  UNIQUE(scope_id, entity_id, lens_id)
);
CREATE TABLE memory_hit (
  id TEXT PRIMARY KEY,
  scope_id TEXT NOT NULL REFERENCES memory_scope(id),
  run_id TEXT,
  node_id TEXT,
  memory_id TEXT NOT NULL,
  kind TEXT NOT NULL,
  injected INTEGER NOT NULL DEFAULT 0,
  consulted INTEGER NOT NULL DEFAULT 0,
  used INTEGER NOT NULL DEFAULT 0,
  outcome TEXT NOT NULL DEFAULT '',
  ab_arm TEXT NOT NULL DEFAULT '',
  ab_repetition INTEGER NOT NULL DEFAULT 0,
  created_at TEXT NOT NULL
);
CREATE TABLE memory_lens (
  id TEXT PRIMARY KEY,
  scope_id TEXT NOT NULL REFERENCES memory_scope(id),
  name TEXT NOT NULL,
  description TEXT NOT NULL DEFAULT '',
  version INTEGER NOT NULL DEFAULT 1,
  created_at TEXT NOT NULL,
  updated_at TEXT NOT NULL,
  UNIQUE(scope_id, name)
);
CREATE TABLE memory_meta (
  key TEXT PRIMARY KEY,
  value TEXT NOT NULL
);
CREATE TABLE memory_node (
  id TEXT PRIMARY KEY,
  scope_id TEXT NOT NULL REFERENCES memory_scope(id),
  kind TEXT NOT NULL,
  canonical_name TEXT NOT NULL,
  aliases_json TEXT NOT NULL DEFAULT '[]',
  created_at TEXT NOT NULL,
  updated_at TEXT NOT NULL,
  metadata TEXT NOT NULL DEFAULT '{}'
);
CREATE VIRTUAL TABLE memory_node_fts USING fts5(
  node_id UNINDEXED,
  name,
  aliases,
  metadata
);
CREATE TABLE memory_passage (
  id TEXT PRIMARY KEY,
  source_id TEXT NOT NULL REFERENCES memory_source(id),
  scope_id TEXT NOT NULL REFERENCES memory_scope(id),
  ordinal INTEGER NOT NULL,
  start INTEGER NOT NULL DEFAULT 0,
  end INTEGER NOT NULL DEFAULT 0,
  text TEXT NOT NULL,
  text_hash TEXT NOT NULL,
  tokens INTEGER NOT NULL DEFAULT 0,
  status TEXT NOT NULL DEFAULT 'active',
  created_at TEXT NOT NULL,
  UNIQUE(source_id, ordinal)
);
CREATE VIRTUAL TABLE memory_passage_fts USING fts5(
  passage_id UNINDEXED,
  text,
  uri UNINDEXED,
  source_id UNINDEXED
);
CREATE TABLE memory_path (
  id TEXT PRIMARY KEY,
  scope_id TEXT NOT NULL REFERENCES memory_scope(id),
  revision INTEGER NOT NULL DEFAULT 1,
  schema_id TEXT NOT NULL REFERENCES memory_path_schema(id),
  task_type TEXT NOT NULL DEFAULT '',
  state TEXT NOT NULL DEFAULT 'candidate',
  intent_sample TEXT NOT NULL DEFAULT '',
  access_json TEXT NOT NULL DEFAULT '{}',
  created_at TEXT NOT NULL,
  updated_at TEXT NOT NULL
);
CREATE TABLE memory_path_claim (
  path_id TEXT NOT NULL REFERENCES memory_path(id),
  ordinal INTEGER NOT NULL,
  claim_id TEXT NOT NULL,
  claim_revision TEXT NOT NULL DEFAULT '',
  PRIMARY KEY (path_id, ordinal)
);
CREATE TABLE memory_path_event (
  trace_id TEXT PRIMARY KEY,
  path_id TEXT NOT NULL REFERENCES memory_path(id),
  input_hash TEXT NOT NULL DEFAULT '',
  created_at TEXT NOT NULL
);
CREATE TABLE memory_path_input (
  path_id TEXT NOT NULL REFERENCES memory_path(id),
  input_hash TEXT NOT NULL,
  PRIMARY KEY (path_id, input_hash)
);
CREATE TABLE memory_path_schema (
  id TEXT PRIMARY KEY,
  scope_id TEXT NOT NULL REFERENCES memory_scope(id),
  signature TEXT NOT NULL,
  created_at TEXT NOT NULL,
  UNIQUE(scope_id, signature)
);
CREATE TABLE memory_path_stats (
  path_id TEXT PRIMARY KEY REFERENCES memory_path(id),
  success INTEGER NOT NULL DEFAULT 0,
  failure INTEGER NOT NULL DEFAULT 0,
  last_tau INTEGER NOT NULL DEFAULT 0,
  uses INTEGER NOT NULL DEFAULT 0,
  distinct_inputs INTEGER NOT NULL DEFAULT 0
);
CREATE TABLE memory_playbook (
  id TEXT PRIMARY KEY,
  scope_id TEXT NOT NULL REFERENCES memory_scope(id),
  task_type TEXT NOT NULL,
  title TEXT NOT NULL,
  steps_json TEXT NOT NULL,
  preconditions_json TEXT NOT NULL DEFAULT '{}',
  success_count INTEGER NOT NULL DEFAULT 0,
  failure_count INTEGER NOT NULL DEFAULT 0,
  confidence REAL NOT NULL DEFAULT 0.5,
  status TEXT NOT NULL DEFAULT 'active',
  source_ref TEXT,
  evidence_json TEXT NOT NULL DEFAULT '[]',
  created_at TEXT NOT NULL,
  updated_at TEXT NOT NULL
);
CREATE TABLE memory_policy (
  scope_id TEXT NOT NULL REFERENCES memory_scope(id),
  key TEXT NOT NULL,
  value_json TEXT NOT NULL,
  immutable INTEGER NOT NULL DEFAULT 0,
  updated_at TEXT NOT NULL,
  updated_by TEXT NOT NULL,
  PRIMARY KEY (scope_id, key)
);
CREATE TABLE memory_proposal (
  id TEXT PRIMARY KEY,
  scope_id TEXT NOT NULL REFERENCES memory_scope(id),
  kind TEXT NOT NULL,
  payload_json TEXT NOT NULL,
  source_episode_id TEXT,
  evidence_json TEXT NOT NULL DEFAULT '[]',
  confidence REAL NOT NULL DEFAULT 0.5,
  status TEXT NOT NULL DEFAULT 'pending',
  created_at TEXT NOT NULL,
  reviewed_at TEXT,
  reviewed_by TEXT
);
CREATE TABLE memory_scope (
  id TEXT PRIMARY KEY,
  kind TEXT NOT NULL CHECK (kind IN ('project','workspace','institution')),
  parent_id TEXT REFERENCES memory_scope(id),
  name TEXT NOT NULL,
  created_at TEXT NOT NULL,
  metadata TEXT NOT NULL DEFAULT '{}'
);
CREATE TABLE memory_scope_closure (
  ancestor_id TEXT NOT NULL REFERENCES memory_scope(id),
  descendant_id TEXT NOT NULL REFERENCES memory_scope(id),
  depth INTEGER NOT NULL,
  PRIMARY KEY (ancestor_id, descendant_id)
);
CREATE TABLE memory_source (
  id TEXT PRIMARY KEY,
  scope_id TEXT NOT NULL REFERENCES memory_scope(id),
  uri TEXT NOT NULL,
  content_hash TEXT NOT NULL,
  kind TEXT NOT NULL DEFAULT '',
  size INTEGER NOT NULL DEFAULT 0,
  first_seen TEXT NOT NULL,
  last_seen TEXT NOT NULL,
  status TEXT NOT NULL DEFAULT 'active'
);
CREATE TABLE memory_state (
  scope_id TEXT NOT NULL REFERENCES memory_scope(id),
  scope_kind TEXT NOT NULL,
  scope_owner TEXT NOT NULL,
  key TEXT NOT NULL,
  value_json TEXT NOT NULL,
  version INTEGER NOT NULL DEFAULT 1,
  schema_hash TEXT NOT NULL DEFAULT '',
  expires_at TEXT,
  created_at TEXT NOT NULL,
  updated_at TEXT NOT NULL,
  PRIMARY KEY (scope_id, scope_kind, scope_owner, key)
);
CREATE TABLE memory_state_history (
  id TEXT PRIMARY KEY,
  scope_id TEXT NOT NULL REFERENCES memory_scope(id),
  scope_kind TEXT NOT NULL,
  scope_owner TEXT NOT NULL,
  key TEXT NOT NULL,
  version INTEGER NOT NULL,
  value_json TEXT NOT NULL,
  schema_hash TEXT NOT NULL DEFAULT '',
  created_at TEXT NOT NULL
);
CREATE TABLE memory_state_key (
  scope_id TEXT NOT NULL REFERENCES memory_scope(id),
  key TEXT NOT NULL,
  scope_kind TEXT NOT NULL CHECK (scope_kind IN ('project','task_type','item')),
  schema_json TEXT NOT NULL DEFAULT '{}',
  schema_hash TEXT NOT NULL DEFAULT '',
  max_bytes INTEGER NOT NULL DEFAULT 16384,
  ttl_seconds INTEGER NOT NULL DEFAULT 0,
  value_mode TEXT NOT NULL DEFAULT 'replace' CHECK (value_mode IN ('replace','versioned')),
  versioned_keep INTEGER NOT NULL DEFAULT 5,
  created_at TEXT NOT NULL,
  updated_at TEXT NOT NULL,
  PRIMARY KEY (scope_id, key)
);
CREATE TABLE memory_trace (
  trace_id TEXT PRIMARY KEY,
  scope_id TEXT NOT NULL REFERENCES memory_scope(id),
  run_id TEXT NOT NULL,
  node_id TEXT NOT NULL,
  item_id TEXT NOT NULL DEFAULT '',
  task_type TEXT NOT NULL DEFAULT '',
  intent_template_hash TEXT NOT NULL DEFAULT '',
  input_hash TEXT NOT NULL DEFAULT '',
  knowledge_version INTEGER NOT NULL DEFAULT 0,
  model TEXT NOT NULL DEFAULT '',
  tier TEXT NOT NULL DEFAULT '',
  outcome TEXT NOT NULL DEFAULT '',
  anchors_json TEXT NOT NULL DEFAULT '[]',
  steps_json TEXT NOT NULL DEFAULT '[]',
  delivered_json TEXT NOT NULL DEFAULT '[]',
  used_json TEXT NOT NULL DEFAULT '[]',
  truncated_steps INTEGER NOT NULL DEFAULT 0,
  created_at TEXT NOT NULL,
  updated_at TEXT NOT NULL,
  UNIQUE(scope_id, run_id, node_id)
);
CREATE TABLE memory_usage (
  id TEXT PRIMARY KEY,
  scope_id TEXT NOT NULL REFERENCES memory_scope(id),
  run_id TEXT,
  node_id TEXT,
  provider TEXT,
  model TEXT,
  prompt_tokens INTEGER NOT NULL DEFAULT 0,
  completion_tokens INTEGER NOT NULL DEFAULT 0,
  total_tokens INTEGER NOT NULL DEFAULT 0,
  cached_tokens INTEGER NOT NULL DEFAULT 0,
  cache_write_tokens INTEGER NOT NULL DEFAULT 0,
  calls INTEGER NOT NULL DEFAULT 0,
  latency_ms INTEGER NOT NULL DEFAULT 0,
  memory_context_chars INTEGER NOT NULL DEFAULT 0,
  memory_enabled INTEGER NOT NULL DEFAULT 0,
  retries INTEGER NOT NULL DEFAULT 0,
  invalid_citations INTEGER NOT NULL DEFAULT 0,
  outcome TEXT NOT NULL DEFAULT '',
  ab_arm TEXT NOT NULL DEFAULT '',
  ab_repetition INTEGER NOT NULL DEFAULT 0,
  created_at TEXT NOT NULL
);
CREATE TABLE memory_validation (
  id TEXT PRIMARY KEY,
  scope_id TEXT NOT NULL REFERENCES memory_scope(id),
  run_id TEXT,
  node_id TEXT,
  episode_id TEXT NOT NULL,
  validator_id TEXT NOT NULL,
  validator_version TEXT NOT NULL DEFAULT '',
  ok INTEGER NOT NULL DEFAULT 0,
  score REAL,
  reasons_json TEXT NOT NULL DEFAULT '[]',
  artifact_hash TEXT,
  ab_arm TEXT NOT NULL DEFAULT '',
  ab_repetition INTEGER NOT NULL DEFAULT 0,
  created_at TEXT NOT NULL,
  UNIQUE(scope_id, episode_id, validator_id, validator_version)
);
CREATE TABLE memory_working (
  scope_id TEXT NOT NULL REFERENCES memory_scope(id),
  item_id TEXT NOT NULL,
  key TEXT NOT NULL,
  value_json TEXT NOT NULL,
  updated_at TEXT NOT NULL,
  PRIMARY KEY (scope_id, item_id, key)
);
CREATE INDEX idx_memory_alias_node ON memory_alias(scope_id, node_id);
CREATE INDEX idx_memory_claim_path_path ON memory_claim_path(path_id);
CREATE INDEX idx_memory_edge_dormant ON memory_edge(scope_id, status, dormant_at);
CREATE INDEX idx_memory_edge_dst_relation ON memory_edge(dst_id, relation);
CREATE INDEX idx_memory_edge_hash ON memory_edge(scope_id, edge_hash, status);
CREATE INDEX idx_memory_edge_lens_lens ON memory_edge_lens(lens_id, edge_id);
CREATE INDEX idx_memory_edge_scope_status ON memory_edge(scope_id, status, valid_to);
CREATE INDEX idx_memory_edge_src_relation ON memory_edge(src_id, relation);
CREATE INDEX idx_memory_embedding_object ON memory_embedding(scope_id, object_type, object_id);
CREATE INDEX idx_memory_episode_scope_time ON memory_episode(scope_id, created_at);
CREATE INDEX idx_memory_evidence_target ON memory_evidence(scope_id, target_kind, target_id);
CREATE INDEX idx_memory_facet_entity ON memory_facet(scope_id, entity_id, lens_id);
CREATE INDEX idx_memory_hit_memory ON memory_hit(scope_id, memory_id);
CREATE INDEX idx_memory_hit_scope_run ON memory_hit(scope_id, run_id, node_id);
CREATE UNIQUE INDEX idx_memory_hit_unique ON memory_hit(scope_id, run_id, node_id, memory_id);
CREATE INDEX idx_memory_node_scope_kind ON memory_node(scope_id, kind);
CREATE INDEX idx_memory_passage_source ON memory_passage(source_id, status);
CREATE INDEX idx_memory_path_state ON memory_path(scope_id, state, task_type);
CREATE INDEX idx_memory_playbook_task ON memory_playbook(scope_id, task_type, status);
CREATE INDEX idx_memory_policy_scope ON memory_policy(scope_id, key);
CREATE INDEX idx_memory_proposal_episode ON memory_proposal(source_episode_id);
CREATE INDEX idx_memory_proposal_scope_status ON memory_proposal(scope_id, status, created_at);
CREATE INDEX idx_memory_source_uri ON memory_source(scope_id, uri, status);
CREATE INDEX idx_memory_state_history_key ON memory_state_history(scope_id, key, version);
CREATE INDEX idx_memory_state_key_scope ON memory_state_key(scope_id);
CREATE INDEX idx_memory_state_scope ON memory_state(scope_id, scope_kind, scope_owner);
CREATE INDEX idx_memory_usage_scope_time ON memory_usage(scope_id, created_at);
CREATE INDEX idx_memory_validation_episode ON memory_validation(scope_id, episode_id);
CREATE TRIGGER trg_kv_memory_alias_DELETE AFTER DELETE ON memory_alias BEGIN UPDATE memory_meta SET value=CAST(value AS INTEGER)+1 WHERE key='knowledge_version'; END;
CREATE TRIGGER trg_kv_memory_alias_INSERT AFTER INSERT ON memory_alias BEGIN UPDATE memory_meta SET value=CAST(value AS INTEGER)+1 WHERE key='knowledge_version'; END;
CREATE TRIGGER trg_kv_memory_alias_UPDATE AFTER UPDATE ON memory_alias BEGIN UPDATE memory_meta SET value=CAST(value AS INTEGER)+1 WHERE key='knowledge_version'; END;
CREATE TRIGGER trg_kv_memory_claim_path_DELETE AFTER DELETE ON memory_claim_path BEGIN UPDATE memory_meta SET value=CAST(value AS INTEGER)+1 WHERE key='knowledge_version'; END;
CREATE TRIGGER trg_kv_memory_claim_path_INSERT AFTER INSERT ON memory_claim_path BEGIN UPDATE memory_meta SET value=CAST(value AS INTEGER)+1 WHERE key='knowledge_version'; END;
CREATE TRIGGER trg_kv_memory_claim_path_UPDATE AFTER UPDATE ON memory_claim_path BEGIN UPDATE memory_meta SET value=CAST(value AS INTEGER)+1 WHERE key='knowledge_version'; END;
CREATE TRIGGER trg_kv_memory_edge_DELETE AFTER DELETE ON memory_edge BEGIN UPDATE memory_meta SET value=CAST(value AS INTEGER)+1 WHERE key='knowledge_version'; END;
CREATE TRIGGER trg_kv_memory_edge_INSERT AFTER INSERT ON memory_edge BEGIN UPDATE memory_meta SET value=CAST(value AS INTEGER)+1 WHERE key='knowledge_version'; END;
CREATE TRIGGER trg_kv_memory_edge_UPDATE AFTER UPDATE ON memory_edge BEGIN UPDATE memory_meta SET value=CAST(value AS INTEGER)+1 WHERE key='knowledge_version'; END;
CREATE TRIGGER trg_kv_memory_edge_lens_DELETE AFTER DELETE ON memory_edge_lens BEGIN UPDATE memory_meta SET value=CAST(value AS INTEGER)+1 WHERE key='knowledge_version'; END;
CREATE TRIGGER trg_kv_memory_edge_lens_INSERT AFTER INSERT ON memory_edge_lens BEGIN UPDATE memory_meta SET value=CAST(value AS INTEGER)+1 WHERE key='knowledge_version'; END;
CREATE TRIGGER trg_kv_memory_edge_lens_UPDATE AFTER UPDATE ON memory_edge_lens BEGIN UPDATE memory_meta SET value=CAST(value AS INTEGER)+1 WHERE key='knowledge_version'; END;
CREATE TRIGGER trg_kv_memory_evidence_DELETE AFTER DELETE ON memory_evidence BEGIN UPDATE memory_meta SET value=CAST(value AS INTEGER)+1 WHERE key='knowledge_version'; END;
CREATE TRIGGER trg_kv_memory_evidence_INSERT AFTER INSERT ON memory_evidence BEGIN UPDATE memory_meta SET value=CAST(value AS INTEGER)+1 WHERE key='knowledge_version'; END;
CREATE TRIGGER trg_kv_memory_evidence_UPDATE AFTER UPDATE ON memory_evidence BEGIN UPDATE memory_meta SET value=CAST(value AS INTEGER)+1 WHERE key='knowledge_version'; END;
CREATE TRIGGER trg_kv_memory_facet_DELETE AFTER DELETE ON memory_facet BEGIN UPDATE memory_meta SET value=CAST(value AS INTEGER)+1 WHERE key='knowledge_version'; END;
CREATE TRIGGER trg_kv_memory_facet_INSERT AFTER INSERT ON memory_facet BEGIN UPDATE memory_meta SET value=CAST(value AS INTEGER)+1 WHERE key='knowledge_version'; END;
CREATE TRIGGER trg_kv_memory_facet_UPDATE AFTER UPDATE ON memory_facet BEGIN UPDATE memory_meta SET value=CAST(value AS INTEGER)+1 WHERE key='knowledge_version'; END;
CREATE TRIGGER trg_kv_memory_lens_DELETE AFTER DELETE ON memory_lens BEGIN UPDATE memory_meta SET value=CAST(value AS INTEGER)+1 WHERE key='knowledge_version'; END;
CREATE TRIGGER trg_kv_memory_lens_INSERT AFTER INSERT ON memory_lens BEGIN UPDATE memory_meta SET value=CAST(value AS INTEGER)+1 WHERE key='knowledge_version'; END;
CREATE TRIGGER trg_kv_memory_lens_UPDATE AFTER UPDATE ON memory_lens BEGIN UPDATE memory_meta SET value=CAST(value AS INTEGER)+1 WHERE key='knowledge_version'; END;
CREATE TRIGGER trg_kv_memory_node_DELETE AFTER DELETE ON memory_node BEGIN UPDATE memory_meta SET value=CAST(value AS INTEGER)+1 WHERE key='knowledge_version'; END;
CREATE TRIGGER trg_kv_memory_node_INSERT AFTER INSERT ON memory_node BEGIN UPDATE memory_meta SET value=CAST(value AS INTEGER)+1 WHERE key='knowledge_version'; END;
CREATE TRIGGER trg_kv_memory_node_UPDATE AFTER UPDATE ON memory_node BEGIN UPDATE memory_meta SET value=CAST(value AS INTEGER)+1 WHERE key='knowledge_version'; END;
CREATE TRIGGER trg_kv_memory_passage_DELETE AFTER DELETE ON memory_passage BEGIN UPDATE memory_meta SET value=CAST(value AS INTEGER)+1 WHERE key='knowledge_version'; END;
CREATE TRIGGER trg_kv_memory_passage_INSERT AFTER INSERT ON memory_passage BEGIN UPDATE memory_meta SET value=CAST(value AS INTEGER)+1 WHERE key='knowledge_version'; END;
CREATE TRIGGER trg_kv_memory_passage_UPDATE AFTER UPDATE ON memory_passage BEGIN UPDATE memory_meta SET value=CAST(value AS INTEGER)+1 WHERE key='knowledge_version'; END;
CREATE TRIGGER trg_kv_memory_path_DELETE AFTER DELETE ON memory_path BEGIN UPDATE memory_meta SET value=CAST(value AS INTEGER)+1 WHERE key='knowledge_version'; END;
CREATE TRIGGER trg_kv_memory_path_INSERT AFTER INSERT ON memory_path BEGIN UPDATE memory_meta SET value=CAST(value AS INTEGER)+1 WHERE key='knowledge_version'; END;
CREATE TRIGGER trg_kv_memory_path_UPDATE AFTER UPDATE ON memory_path BEGIN UPDATE memory_meta SET value=CAST(value AS INTEGER)+1 WHERE key='knowledge_version'; END;
CREATE TRIGGER trg_kv_memory_path_claim_DELETE AFTER DELETE ON memory_path_claim BEGIN UPDATE memory_meta SET value=CAST(value AS INTEGER)+1 WHERE key='knowledge_version'; END;
CREATE TRIGGER trg_kv_memory_path_claim_INSERT AFTER INSERT ON memory_path_claim BEGIN UPDATE memory_meta SET value=CAST(value AS INTEGER)+1 WHERE key='knowledge_version'; END;
CREATE TRIGGER trg_kv_memory_path_claim_UPDATE AFTER UPDATE ON memory_path_claim BEGIN UPDATE memory_meta SET value=CAST(value AS INTEGER)+1 WHERE key='knowledge_version'; END;
CREATE TRIGGER trg_kv_memory_path_event_DELETE AFTER DELETE ON memory_path_event BEGIN UPDATE memory_meta SET value=CAST(value AS INTEGER)+1 WHERE key='knowledge_version'; END;
CREATE TRIGGER trg_kv_memory_path_event_INSERT AFTER INSERT ON memory_path_event BEGIN UPDATE memory_meta SET value=CAST(value AS INTEGER)+1 WHERE key='knowledge_version'; END;
CREATE TRIGGER trg_kv_memory_path_event_UPDATE AFTER UPDATE ON memory_path_event BEGIN UPDATE memory_meta SET value=CAST(value AS INTEGER)+1 WHERE key='knowledge_version'; END;
CREATE TRIGGER trg_kv_memory_path_input_DELETE AFTER DELETE ON memory_path_input BEGIN UPDATE memory_meta SET value=CAST(value AS INTEGER)+1 WHERE key='knowledge_version'; END;
CREATE TRIGGER trg_kv_memory_path_input_INSERT AFTER INSERT ON memory_path_input BEGIN UPDATE memory_meta SET value=CAST(value AS INTEGER)+1 WHERE key='knowledge_version'; END;
CREATE TRIGGER trg_kv_memory_path_input_UPDATE AFTER UPDATE ON memory_path_input BEGIN UPDATE memory_meta SET value=CAST(value AS INTEGER)+1 WHERE key='knowledge_version'; END;
CREATE TRIGGER trg_kv_memory_path_schema_DELETE AFTER DELETE ON memory_path_schema BEGIN UPDATE memory_meta SET value=CAST(value AS INTEGER)+1 WHERE key='knowledge_version'; END;
CREATE TRIGGER trg_kv_memory_path_schema_INSERT AFTER INSERT ON memory_path_schema BEGIN UPDATE memory_meta SET value=CAST(value AS INTEGER)+1 WHERE key='knowledge_version'; END;
CREATE TRIGGER trg_kv_memory_path_schema_UPDATE AFTER UPDATE ON memory_path_schema BEGIN UPDATE memory_meta SET value=CAST(value AS INTEGER)+1 WHERE key='knowledge_version'; END;
CREATE TRIGGER trg_kv_memory_path_stats_DELETE AFTER DELETE ON memory_path_stats BEGIN UPDATE memory_meta SET value=CAST(value AS INTEGER)+1 WHERE key='knowledge_version'; END;
CREATE TRIGGER trg_kv_memory_path_stats_INSERT AFTER INSERT ON memory_path_stats BEGIN UPDATE memory_meta SET value=CAST(value AS INTEGER)+1 WHERE key='knowledge_version'; END;
CREATE TRIGGER trg_kv_memory_path_stats_UPDATE AFTER UPDATE ON memory_path_stats BEGIN UPDATE memory_meta SET value=CAST(value AS INTEGER)+1 WHERE key='knowledge_version'; END;
CREATE TRIGGER trg_kv_memory_playbook_DELETE AFTER DELETE ON memory_playbook BEGIN UPDATE memory_meta SET value=CAST(value AS INTEGER)+1 WHERE key='knowledge_version'; END;
CREATE TRIGGER trg_kv_memory_playbook_INSERT AFTER INSERT ON memory_playbook BEGIN UPDATE memory_meta SET value=CAST(value AS INTEGER)+1 WHERE key='knowledge_version'; END;
CREATE TRIGGER trg_kv_memory_playbook_UPDATE AFTER UPDATE ON memory_playbook BEGIN UPDATE memory_meta SET value=CAST(value AS INTEGER)+1 WHERE key='knowledge_version'; END;
CREATE TRIGGER trg_kv_memory_scope_DELETE AFTER DELETE ON memory_scope BEGIN UPDATE memory_meta SET value=CAST(value AS INTEGER)+1 WHERE key='knowledge_version'; END;
CREATE TRIGGER trg_kv_memory_scope_INSERT AFTER INSERT ON memory_scope BEGIN UPDATE memory_meta SET value=CAST(value AS INTEGER)+1 WHERE key='knowledge_version'; END;
CREATE TRIGGER trg_kv_memory_scope_UPDATE AFTER UPDATE ON memory_scope BEGIN UPDATE memory_meta SET value=CAST(value AS INTEGER)+1 WHERE key='knowledge_version'; END;
CREATE TRIGGER trg_kv_memory_source_DELETE AFTER DELETE ON memory_source BEGIN UPDATE memory_meta SET value=CAST(value AS INTEGER)+1 WHERE key='knowledge_version'; END;
CREATE TRIGGER trg_kv_memory_source_INSERT AFTER INSERT ON memory_source BEGIN UPDATE memory_meta SET value=CAST(value AS INTEGER)+1 WHERE key='knowledge_version'; END;
CREATE TRIGGER trg_kv_memory_source_UPDATE AFTER UPDATE ON memory_source BEGIN UPDATE memory_meta SET value=CAST(value AS INTEGER)+1 WHERE key='knowledge_version'; END;
CREATE TRIGGER trg_kv_memory_state_DELETE AFTER DELETE ON memory_state BEGIN UPDATE memory_meta SET value=CAST(value AS INTEGER)+1 WHERE key='knowledge_version'; END;
CREATE TRIGGER trg_kv_memory_state_INSERT AFTER INSERT ON memory_state BEGIN UPDATE memory_meta SET value=CAST(value AS INTEGER)+1 WHERE key='knowledge_version'; END;
CREATE TRIGGER trg_kv_memory_state_UPDATE AFTER UPDATE ON memory_state BEGIN UPDATE memory_meta SET value=CAST(value AS INTEGER)+1 WHERE key='knowledge_version'; END;
CREATE TRIGGER trg_kv_memory_state_history_DELETE AFTER DELETE ON memory_state_history BEGIN UPDATE memory_meta SET value=CAST(value AS INTEGER)+1 WHERE key='knowledge_version'; END;
CREATE TRIGGER trg_kv_memory_state_history_INSERT AFTER INSERT ON memory_state_history BEGIN UPDATE memory_meta SET value=CAST(value AS INTEGER)+1 WHERE key='knowledge_version'; END;
CREATE TRIGGER trg_kv_memory_state_history_UPDATE AFTER UPDATE ON memory_state_history BEGIN UPDATE memory_meta SET value=CAST(value AS INTEGER)+1 WHERE key='knowledge_version'; END;
CREATE TRIGGER trg_kv_memory_working_DELETE AFTER DELETE ON memory_working BEGIN UPDATE memory_meta SET value=CAST(value AS INTEGER)+1 WHERE key='knowledge_version'; END;
CREATE TRIGGER trg_kv_memory_working_INSERT AFTER INSERT ON memory_working BEGIN UPDATE memory_meta SET value=CAST(value AS INTEGER)+1 WHERE key='knowledge_version'; END;
CREATE TRIGGER trg_kv_memory_working_UPDATE AFTER UPDATE ON memory_working BEGIN UPDATE memory_meta SET value=CAST(value AS INTEGER)+1 WHERE key='knowledge_version'; END;
