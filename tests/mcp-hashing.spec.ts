import { describe, it, expect, beforeEach } from 'vitest';
import { McpHasher } from '../src/core/skills/McpHasher.ts';
import { McpLedgerService, NATIVE_EMBEDDED_MCPS } from '../src/core/skills/McpLedgerService.ts';
import { APPROVED_WORKSPACE_MCPS } from '../src/core/workspace/ApprovedWorkspaceCatalog.ts';
import { EventBus } from '../src/core/EventBus.ts';
import { IMcpServerConfig } from '../src/core/types.ts';

describe('Governança MCP - Fase 1: Hashing Criptográfico Canônico e Ledger MCP', () => {
  let bus: EventBus;
  let ledger: McpLedgerService;

  beforeEach(() => {
    bus = new EventBus();
    ledger = McpLedgerService.resetForTesting(bus);
  });

  describe('McpHasher - Normalização e Digest Canônico', () => {
    it('deve normalizar quebras de linha Windows (CRLF) para LF em descrições de ferramentas', () => {
      const crlfText = 'Ferramenta de busca de dados\r\nParâmetros obrigatórios:\r\n- id\r\n';
      const normalized = McpHasher.normalizeText(crlfText);
      expect(normalized).toBe('Ferramenta de busca de dados\nParâmetros obrigatórios:\n- id');
      expect(normalized).not.toContain('\r');
    });

    it('deve serializar objetos JSON com chaves ordenadas alfabeticamente', () => {
      const schemaA = { z: 1, a: 2, m: { y: 'bar', b: 'foo' } };
      const schemaB = { a: 2, m: { b: 'foo', y: 'bar' }, z: 1 };

      const canonA = McpHasher.canonicalizeJson(schemaA);
      const canonB = McpHasher.canonicalizeJson(schemaB);

      expect(canonA).toBe(canonB);
      expect(canonA).toBe('{"a":2,"m":{"b":"foo","y":"bar"},"z":1}');
    });

    it('deve gerar o mesmo hash SHA-256 independentemente de CRLF vs LF nas descrições de ferramentas', () => {
      const serverCrlf: Partial<IMcpServerConfig> = {
        id: 'weather-server',
        transport: 'stdio',
        command: 'npx',
        args: ['@mcp/weather@1.0.0'],
        cachedTools: [
          {
            name: 'get_weather',
            description: 'Obter previsão do tempo.\r\nSuporta cidades globais.\r\n',
            serverId: 'weather-server'
          }
        ]
      };

      const serverLf: Partial<IMcpServerConfig> = {
        id: 'weather-server',
        transport: 'stdio',
        command: 'npx',
        args: ['@mcp/weather@1.0.0'],
        cachedTools: [
          {
            name: 'get_weather',
            description: 'Obter previsão do tempo.\nSuporta cidades globais.',
            serverId: 'weather-server'
          }
        ]
      };

      const hashCrlf = McpHasher.computeHash(serverCrlf);
      const hashLf = McpHasher.computeHash(serverLf);

      expect(hashCrlf).toHaveLength(64);
      expect(hashLf).toHaveLength(64);
      expect(hashCrlf).toBe(hashLf);
    });

    it('deve gerar o mesmo hash independentemente da ordem em que as ferramentas forem retornadas pelo servidor', () => {
      const server1: Partial<IMcpServerConfig> = {
        id: 'db-server',
        cachedTools: [
          { name: 'query', description: 'Executar consulta SQL' },
          { name: 'connect', description: 'Conectar ao banco' }
        ]
      };

      const server2: Partial<IMcpServerConfig> = {
        id: 'db-server',
        cachedTools: [
          { name: 'connect', description: 'Conectar ao banco' },
          { name: 'query', description: 'Executar consulta SQL' }
        ]
      };

      expect(McpHasher.computeHash(server1)).toBe(McpHasher.computeHash(server2));
    });

    it('deve alterar o hash completamente se 1 único caractere for alterado na descrição de uma ferramenta (anti-tool poisoning)', () => {
      const benign: Partial<IMcpServerConfig> = {
        id: 'safe-tool',
        cachedTools: [
          { name: 'calculate', description: 'Calcula fórmulas matemáticas com precisão.' }
        ]
      };

      const poisoned: Partial<IMcpServerConfig> = {
        id: 'safe-tool',
        cachedTools: [
          { name: 'calculate', description: 'Calcula fórmulas matemáticas com precisão!' }
        ]
      };

      const hashA = McpHasher.computeHash(benign);
      const hashB = McpHasher.computeHash(poisoned);

      expect(hashA).not.toBe(hashB);
    });

    it('deve alterar o hash se os argumentos ou versão do comando forem modificados', () => {
      const versionPinned: Partial<IMcpServerConfig> = {
        id: 'my-service',
        command: 'npx',
        args: ['@modelcontextprotocol/server-postgres@0.1.2']
      };

      const unpinnedOrChanged: Partial<IMcpServerConfig> = {
        id: 'my-service',
        command: 'npx',
        args: ['@modelcontextprotocol/server-postgres@0.1.3']
      };

      expect(McpHasher.computeHash(versionPinned)).not.toBe(McpHasher.computeHash(unpinnedOrChanged));
    });
  });

  describe('McpLedgerService - Registro e Consulta O(1)', () => {
    it('deve auto-homologar todos os servidores nativos em C++ (arestrix.*) com risco 0', () => {
      for (const native of NATIVE_EMBEDDED_MCPS) {
        const mockConfig: Partial<IMcpServerConfig> = {
          id: native.id,
          name: native.name,
          transport: 'stdio',
          command: 'native-core',
          args: [],
          isEnabled: true,
          cachedTools: native.tools.map(t => ({ name: t, description: `Ferramenta nativa C++ ${t}`, serverId: native.id }))
        };
        const hash = McpHasher.computeHash(mockConfig);
        const entry = ledger.getEntry(hash);

        expect(entry).toBeDefined();
        expect(entry?.verdict).toBe('APPROVE');
        expect(entry?.riskScore).toBe(0);
        expect(entry?.provenance).toBe('native-embedded');
        expect(ledger.isApproved(hash)).toBe(true);
      }
    });

    it('deve auto-homologar o catálogo oficial de MCPs do workspace', () => {
      for (const mcp of APPROVED_WORKSPACE_MCPS) {
        const mockConfig: Partial<IMcpServerConfig> = {
          id: mcp.id,
          name: mcp.name,
          transport: mcp.transport,
          command: mcp.command,
          args: mcp.args,
          isEnabled: true,
          cachedTools: mcp.tools.map(t => ({ name: t.name, description: t.description, serverId: mcp.id }))
        };
        const hash = McpHasher.computeHash(mockConfig);
        const entry = ledger.getEntry(hash);

        expect(entry).toBeDefined();
        expect(entry?.verdict).toBe('APPROVE');
        expect(entry?.riskScore).toBe(0);
        expect(entry?.provenance).toBe('workspace-curated');
      }
    });

    it('deve registrar aprovação manual e emitir evento mcps:ledger-updated', () => {
      let eventPayload: any = null;
      bus.on('mcps:ledger-updated', payload => {
        eventPayload = payload;
      });

      const customServer: Partial<IMcpServerConfig> = {
        id: 'external-service',
        name: 'Serviço Externo Seguro',
        transport: 'stdio',
        command: 'uvx',
        args: ['mcp-server-fetch@1.2.0']
      };

      const entry = ledger.recordApproval(customServer, {
        auditedBy: 'SecTeam',
        riskScore: 5
      });

      expect(entry.verdict).toBe('APPROVE');
      expect(entry.riskScore).toBe(5);
      expect(ledger.isApproved(entry.hash)).toBe(true);
      expect(eventPayload).toEqual({
        hash: entry.hash,
        serverId: 'external-service',
        verdict: 'APPROVE'
      });
    });

    it('deve registrar rejeição e bloquear status no ledger', () => {
      const maliciousServer: Partial<IMcpServerConfig> = {
        id: 'malicious-server',
        name: 'Servidor Suspeito',
        command: 'cmd.exe',
        args: ['/c', 'dir & echo hacked']
      };

      const entry = ledger.recordRejection(maliciousServer, 'Uso de metacaracteres perigosos de shell.');

      expect(entry.verdict).toBe('REJECT');
      expect(entry.riskScore).toBeGreaterThanOrEqual(90);
      expect(ledger.isApproved(entry.hash)).toBe(false);
      expect(ledger.getStatus(entry.hash)).toBe('REJECT');
    });

    it('deve reconhecer corretamente servidores nativos C++ via isNativeCoreServer', () => {
      expect(ledger.isNativeCoreServer('arestrix.arquivos')).toBe(true);
      expect(ledger.isNativeCoreServer('arestrix.http')).toBe(true);
      expect(ledger.isNativeCoreServer('external-rag')).toBe(false);
    });
  });
});
