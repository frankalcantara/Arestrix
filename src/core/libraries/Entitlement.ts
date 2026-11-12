import type {
  ILibraryAccessPolicy,
  ILibraryAsset,
  ILibraryEntitlementClaim,
  ILibrarySignedEntitlement,
  LibraryCommercialTier
} from './types.ts';

const TIER_RANK: Record<LibraryCommercialTier, number> = {
  free: 0,
  pro: 1,
  enterprise: 2
};

export interface IEntitlementCheck {
  valid: boolean;
  reason?: string;
  claim?: ILibraryEntitlementClaim;
}

export interface IEntitlementDecision {
  allowed: boolean;
  reason: string;
  claim?: ILibraryEntitlementClaim;
}

/** JSON canônico com chaves ordenadas; é o formato coberto pela assinatura. */
export function canonicalJson(value: unknown): string {
  if (Array.isArray(value)) {
    return `[${value.map(item => canonicalJson(item)).join(',')}]`;
  }
  if (value && typeof value === 'object') {
    const entries = Object.entries(value as Record<string, unknown>)
      .filter(([, item]) => item !== undefined)
      .sort(([left], [right]) => left < right ? -1 : left > right ? 1 : 0);
    return `{${entries.map(([key, item]) => `${JSON.stringify(key)}:${canonicalJson(item)}`).join(',')}}`;
  }
  return JSON.stringify(value) ?? 'null';
}

function decodeBase64Url(value: string): ArrayBuffer {
  const normalized = value.replace(/-/g, '+').replace(/_/g, '/');
  const padded = normalized + '='.repeat((4 - (normalized.length % 4)) % 4);
  const binary = atob(padded);
  const bytes = new Uint8Array(binary.length);
  for (let index = 0; index < binary.length; index += 1) {
    bytes[index] = binary.charCodeAt(index);
  }
  return bytes.buffer as ArrayBuffer;
}

function pemToDer(pem: string): ArrayBuffer {
  const body = pem
    .replace(/-----BEGIN [^-]+-----/g, '')
    .replace(/-----END [^-]+-----/g, '')
    .replace(/\s+/g, '');
  if (!body) throw new Error('Chave pública vazia.');
  const binary = atob(body);
  const bytes = new Uint8Array(binary.length);
  for (let index = 0; index < binary.length; index += 1) {
    bytes[index] = binary.charCodeAt(index);
  }
  return bytes.buffer as ArrayBuffer;
}

function claimError(claim: Partial<ILibraryEntitlementClaim> | undefined): string | undefined {
  if (!claim || typeof claim !== 'object') return 'Claim de entitlement ausente.';
  if (!claim.productId || typeof claim.productId !== 'string') return 'productId ausente.';
  if (!claim.subject || typeof claim.subject !== 'string') return 'subject ausente.';
  if (!claim.tier || !(claim.tier in TIER_RANK)) return 'tier inválido.';
  if (!claim.issuedAt || Number.isNaN(Date.parse(claim.issuedAt))) return 'issuedAt inválido.';
  if (claim.expiresAt && Number.isNaN(Date.parse(claim.expiresAt))) return 'expiresAt inválido.';
  return undefined;
}

/**
 * Verifica uma licença offline assinada com Ed25519. A assinatura cobre o JSON
 * canônico da claim; a chave pública é fornecida pela instalação/organização.
 */
export async function verifyEntitlement(
  entitlement: ILibrarySignedEntitlement | null | undefined,
  publicKeyPem: string,
  now: Date = new Date()
): Promise<IEntitlementCheck> {
  if (!entitlement || entitlement.format !== 'arestrix-entitlement' || entitlement.version !== 1) {
    return { valid: false, reason: 'Formato de entitlement inválido.' };
  }
  if (!publicKeyPem?.trim()) {
    return { valid: false, reason: 'Chave pública de entitlement não configurada.' };
  }
  const error = claimError(entitlement.claim);
  if (error) return { valid: false, reason: error };
  if (!entitlement.signature) return { valid: false, reason: 'Assinatura ausente.' };

  if (!globalThis.crypto?.subtle) {
    return { valid: false, reason: 'WebCrypto indisponível para verificar a assinatura.' };
  }

  try {
    const key = await globalThis.crypto.subtle.importKey(
      'spki',
      pemToDer(publicKeyPem),
      { name: 'Ed25519' },
      false,
      ['verify']
    );
    const valid = await globalThis.crypto.subtle.verify(
      { name: 'Ed25519' },
      key,
      decodeBase64Url(entitlement.signature),
      new TextEncoder().encode(canonicalJson(entitlement.claim))
    );
    if (!valid) return { valid: false, reason: 'Assinatura criptográfica inválida.' };
  } catch (error: any) {
    return { valid: false, reason: `Falha ao verificar assinatura: ${error?.message || String(error)}` };
  }

  const claim = entitlement.claim;
  if (claim.expiresAt && Date.parse(claim.expiresAt) <= now.getTime()) {
    return { valid: false, reason: 'Entitlement expirado.' };
  }
  return { valid: true, claim };
}

/** Aplica a política comercial do asset a uma claim já verificada. */
export function evaluateAssetEntitlement(
  asset: Pick<ILibraryAsset, 'id' | 'access'>,
  check: IEntitlementCheck | null | undefined,
  now: Date = new Date()
): IEntitlementDecision {
  const access: ILibraryAccessPolicy | undefined = asset.access;
  if (!access?.requiresEntitlement) {
    return { allowed: true, reason: 'Asset sem exigência de entitlement.' };
  }
  if (!check?.valid || !check.claim) {
    return { allowed: false, reason: check?.reason || 'Entitlement ausente ou inválido.' };
  }
  const claim = check.claim;
  if (access.productId && claim.productId !== access.productId) {
    return { allowed: false, reason: 'Entitlement de outro produto.', claim };
  }
  if (access.tier && TIER_RANK[claim.tier] < TIER_RANK[access.tier]) {
    return { allowed: false, reason: `Entitlement ${claim.tier} insuficiente para ${access.tier}.`, claim };
  }
  if (claim.assetIds?.length && !claim.assetIds.includes(asset.id)) {
    return { allowed: false, reason: 'Entitlement não cobre este asset.', claim };
  }
  if (claim.expiresAt && Date.parse(claim.expiresAt) <= now.getTime()) {
    return { allowed: false, reason: 'Entitlement expirado.', claim };
  }
  return { allowed: true, reason: 'Entitlement válido.', claim };
}
