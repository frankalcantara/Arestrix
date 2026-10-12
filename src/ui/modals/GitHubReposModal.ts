import { BaseModal } from './BaseModal.ts';
import { GitHubSkillsRegistry } from '../../core/skills/GitHubSkillsRegistry.ts';
import { EventBus } from '../../core/EventBus.ts';

/**
 * Arestrix Studio - Modal Global de Repositórios Open Source do GitHub
 * 
 * Configuração global do sistema: gerencia quais repositórios open source
 * do GitHub fornecem habilidades (Skills) ao catálogo global do Arestrix.
 * No nó de cada agente, o usuário apenas escolhe qual skill importar.
 */
export class GitHubReposModal extends BaseModal {
  private registry: GitHubSkillsRegistry;
  private bus?: EventBus;
  private notificationMessage: string | null = null;
  private notificationType: 'success' | 'warning' = 'success';

  constructor(bus?: EventBus) {
    super({
      id: 'github-repos-modal',
      title: 'Repositórios do GitHub (Catálogo Global de Skills)',
      subtitle: 'Gerencie os projetos open source autorizados que alimentam a biblioteca global de Skills',
      icon: '🌐',
      minWidth: 740,
      minHeight: 540,
      dialogClass: 'github-repos-modal-dialog'
    });

    this.bus = bus;
    this.registry = GitHubSkillsRegistry.getInstance();
  }

  public override open(): void {
    this.notificationMessage = null;
    super.open();
  }

  protected renderBody(): string {
    const reposList = this.registry.getRepositories();

    return `
      <div style="padding: 16px 20px;">
        <!-- CALLOUT DE INFORMAÇÃO GLOBAL -->
        <div class="office-info-callout" style="margin-bottom: 16px; background: #f0f7ff; border: 1px solid #c7e0f4; border-radius: 4px; padding: 12px 16px; display: flex; align-items: flex-start; gap: 12px;">
          <span style="font-size: 20px; line-height: 1;">🌐</span>
          <div style="font-size: 12px; color: #242424; line-height: 1.45;">
            <strong>Governança Global de Repositórios:</strong> Estes repositórios open source fornecem habilidades prontas para todo o workspace.
            Habilitar ou desabilitar um projeto aqui afeta a disponibilidade de suas skills no catálogo de importação de todos os vértices de agentes.
          </div>
        </div>

        ${this.notificationMessage ? `
          <div class="office-callout ${this.notificationType === 'success' ? 'callout-success' : 'callout-warning'}" style="margin-bottom: 14px; padding: 10px 14px; border-radius: 4px; font-size: 12px;">
            <span>${this.escapeHtml(this.notificationMessage)}</span>
          </div>
        ` : ''}

        <!-- TABELA DE REPOSITÓRIOS GLOBAIS CADASTRADOS -->
        <div class="office-section-card" style="margin-bottom: 18px; padding: 16px;">
          <div style="display: flex; justify-content: space-between; align-items: center; margin-bottom: 12px;">
            <h3 style="font-size: 13px; font-weight: 700; color: #1e293b; margin: 0;">
              Projetos Open Source Autorizados (${reposList.length})
            </h3>
            <span style="font-size: 11px; color: #64748b;">
              ${reposList.filter(r => r.isEnabled).length} ativos no catálogo
            </span>
          </div>

          <div style="background: #ffffff; border: 1px solid #e2e8f0; border-radius: 4px; overflow: hidden;">
            <table style="width: 100%; border-collapse: collapse; font-size: 12px; text-align: left;">
              <thead>
                <tr style="background: #f8fafc; border-bottom: 1px solid #e2e8f0; color: #475569; font-weight: 600;">
                  <th style="padding: 10px 12px; width: 60px; text-align: center;">Ativo</th>
                  <th style="padding: 10px 12px;">Repositório / Nome</th>
                  <th style="padding: 10px 12px; width: 110px;">Licença</th>
                  <th style="padding: 10px 12px; width: 80px; text-align: center;">Ações</th>
                </tr>
              </thead>
              <tbody>
                ${reposList.map(repo => `
                  <tr style="border-bottom: 1px solid #f1f5f9; transition: background 0.15s ease;">
                    <td style="padding: 10px 12px; text-align: center;">
                      <input type="checkbox" class="chk-global-toggle-repo" data-repo-id="${repo.id}" ${repo.isEnabled ? 'checked' : ''} style="cursor: pointer;" title="${repo.isEnabled ? 'Desativar este repositório do catálogo' : 'Ativar este repositório no catálogo'}" />
                    </td>
                    <td style="padding: 10px 12px;">
                      <div style="font-weight: 700; color: #0f172a;">${this.escapeHtml(repo.name)}</div>
                      <div style="font-size: 11px; color: #64748b; margin-top: 2px;">
                        <code style="background: #f1f5f9; padding: 2px 4px; border-radius: 3px; font-family: monospace;">${this.escapeHtml(repo.repo)}</code>
                        &bull; ${this.escapeHtml(repo.description)}
                        &bull; <a href="https://github.com/${repo.repo}" target="_blank" rel="noopener noreferrer" style="color: #0f6cbd; text-decoration: none; font-weight: 600;">Abrir no GitHub ↗</a>
                      </div>
                    </td>
                    <td style="padding: 10px 12px;">
                      <span class="office-license-badge" style="background: #eff6fc; color: #0f6cbd; border: 1px solid #c7e0f4; border-radius: 3px; font-size: 11px; font-weight: 700; padding: 2px 6px; display: inline-block;">
                        ${this.escapeHtml(repo.license)}
                      </span>
                    </td>
                    <td style="padding: 10px 12px; text-align: center;">
                      ${!repo.isDefault ? `
                        <button class="btn btn-danger btn-sm btn-global-remove-repo" data-repo-id="${repo.id}" style="padding: 2px 8px; font-size: 11px;">
                          Excluir
                        </button>
                      ` : `
                        <span style="font-size: 11px; color: #94a3b8; font-weight: 600;">Padrão</span>
                      `}
                    </td>
                  </tr>
                `).join('')}
              </tbody>
            </table>
          </div>
        </div>

        <!-- FORMULÁRIO DE CADASTRO DE NOVO REPOSITÓRIO GITHUB -->
        <div class="office-section-card" style="padding: 16px;">
          <h4 style="font-size: 13px; font-weight: 700; color: #1e293b; margin: 0 0 10px 0;">
            ＋ Cadastrar Novo Repositório Open Source do GitHub
          </h4>
          <p style="font-size: 11px; color: #64748b; margin: 0 0 12px 0;">
            Adicione novos repositórios públicos que contenham habilidades com licenças permissivas (MIT, Apache-2.0, BSD).
          </p>

          <div class="form-row-grid" style="grid-template-columns: 2fr 1fr; gap: 12px; margin-bottom: 10px;">
            <div class="form-group" style="margin: 0;">
              <label class="form-label" for="input-new-global-repo-path">Caminho ou URL do Repositório</label>
              <input type="text" id="input-new-global-repo-path" class="form-input" placeholder="ex: usuario/projeto ou https://github.com/usuario/projeto" />
            </div>
            <div class="form-group" style="margin: 0;">
              <label class="form-label" for="select-new-global-repo-license">Licença Permissiva</label>
              <select id="select-new-global-repo-license" class="form-select">
                <option value="MIT">MIT</option>
                <option value="Apache-2.0">Apache-2.0</option>
                <option value="BSD-3-Clause">BSD-3-Clause</option>
                <option value="BSD-2-Clause">BSD-2-Clause</option>
                <option value="ISC">ISC</option>
              </select>
            </div>
          </div>

          <div class="form-row-grid" style="grid-template-columns: 1fr 2fr; gap: 12px; margin-bottom: 12px;">
            <div class="form-group" style="margin: 0;">
              <label class="form-label" for="input-new-global-repo-name">Nome Amigável no Catálogo</label>
              <input type="text" id="input-new-global-repo-name" class="form-input" placeholder="ex: Habilidades da Comunidade" />
            </div>
            <div class="form-group" style="margin: 0;">
              <label class="form-label" for="input-new-global-repo-desc">Descrição Resumida</label>
              <input type="text" id="input-new-global-repo-desc" class="form-input" placeholder="ex: Conjunto de prompts e habilidades especializadas" />
            </div>
          </div>

          <div style="display: flex; justify-content: flex-end;">
            <button class="btn btn-secondary" id="btn-add-global-repo" style="font-weight: 600; min-width: 160px;">
              ＋ Adicionar ao Catálogo Global
            </button>
          </div>
        </div>
      </div>
    `;
  }

  protected override renderFooter(): string {
    return `
      <div class="modal-footer" style="padding: 12px 20px; border-top: 1px solid #e2e8f0; display: flex; justify-content: flex-end; gap: 8px;">
        <button class="btn btn-primary" id="btn-close-global-repos" style="min-width: 100px;">
          Fechar
        </button>
      </div>
    `;
  }

  protected override bindEvents(): void {
    // Fechar pelo botão do footer
    this.dialog.querySelector('#btn-close-global-repos')?.addEventListener('click', () => {
      this.hide();
    });

    // Alternar ativação de repositório
    this.dialog.querySelectorAll<HTMLInputElement>('.chk-global-toggle-repo').forEach(chk => {
      chk.addEventListener('change', () => {
        const repoId = chk.dataset.repoId;
        if (!repoId) return;
        this.registry.toggleRepository(repoId, chk.checked);
        this.notificationMessage = chk.checked 
          ? 'Repositório ativado no catálogo global de skills!' 
          : 'Repositório desativado do catálogo global de skills.';
        this.notificationType = 'success';
        this.bus?.emit('skills:repositories-updated');
        this.render();
      });
    });

    // Excluir repositório personalizado
    this.dialog.querySelectorAll<HTMLButtonElement>('.btn-global-remove-repo').forEach(btn => {
      btn.addEventListener('click', () => {
        const repoId = btn.dataset.repoId;
        if (!repoId) return;
        const removed = this.registry.removeRepository(repoId);
        if (removed) {
          this.notificationMessage = 'Repositório removido com sucesso.';
          this.notificationType = 'success';
          this.bus?.emit('skills:repositories-updated');
          this.render();
        }
      });
    });

    // Adicionar novo repositório
    this.dialog.querySelector('#btn-add-global-repo')?.addEventListener('click', () => {
      const pathInput = this.dialog.querySelector('#input-new-global-repo-path') as HTMLInputElement;
      const nameInput = this.dialog.querySelector('#input-new-global-repo-name') as HTMLInputElement;
      const descInput = this.dialog.querySelector('#input-new-global-repo-desc') as HTMLInputElement;
      const licenseSelect = this.dialog.querySelector('#select-new-global-repo-license') as HTMLSelectElement;

      if (!pathInput || !pathInput.value.trim()) {
        this.notificationMessage = 'Informe o caminho ou URL do repositório no GitHub (ex: organizacao/projeto).';
        this.notificationType = 'warning';
        this.render();
        return;
      }

      const success = this.registry.addRepository(
        pathInput.value,
        nameInput?.value,
        descInput?.value,
        licenseSelect?.value || 'MIT'
      );

      if (success) {
        this.notificationMessage = 'Novo repositório cadastrado com sucesso no catálogo global!';
        this.notificationType = 'success';
        this.bus?.emit('skills:repositories-updated');
        this.render();
      } else {
        this.notificationMessage = 'Não foi possível cadastrar o repositório. Verifique se o formato está correto (usuario/projeto) ou se o repositório já existe.';
        this.notificationType = 'warning';
        this.render();
      }
    });
  }

  private escapeHtml(str: string): string {
    return str
      .replace(/&/g, '&amp;')
      .replace(/</g, '&lt;')
      .replace(/>/g, '&gt;')
      .replace(/"/g, '&quot;')
      .replace(/'/g, '&#039;');
  }
}
