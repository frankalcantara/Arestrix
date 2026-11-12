import { describe, expect, it } from 'vitest';
import { webcrypto } from 'node:crypto';
import {
  DEFAULT_LICENSE_ALLOWLIST,
  normalizeLicense,
  normalizeLicenseAllowlist,
  validateLicense
} from '../src/core/libraries/LicensePolicy.ts';
import {
  canonicalJson,
  evaluateAssetEntitlement,
  verifyEntitlement
} from '../src/core/libraries/Entitlement.ts';
import type { ILibraryEntitlementClaim, ILibrarySignedEntitlement } from '../src/core/libraries/types.ts';

const subtle = (globalThis.crypto || webcrypto).subtle;

function toBase64Url(bytes: Uint8Array): string {
  return Buffer.from(bytes).toString('base64url');
}

function toPublicPem(der: ArrayBuffer): string {
  const body = Buffer.from(der).toString('base64').match(/.{1,64}/g)?.join('\n') || '';
  return `-----BEGIN PUBLIC KEY-----\n${body}\n-----END PUBLIC KEY-----`;
}

async function signedEntitlement(
  claim: ILibraryEntitlementClaim,
  privateKey: CryptoKey
): Promise<ILibrarySignedEntitlement> {
  const signature = await subtle.sign(
    { name: 'Ed25519' },
    privateKey,
    new TextEncoder().encode(canonicalJson(claim))
  );
  return {
    format: 'arestrix-entitlement',
    version: 1,
    claim,
    signature: toBase64Url(new Uint8Array(signature))
  };
}

describe('LicensePolicy - normalização e allowlist SPDX', () => {
  it('normaliza aliases e expressões comuns', () => {
    expect(normalizeLicense('MIT')).toBe('MIT');
    expect(normalizeLicense('mit license')).toBe('MIT');
    expect(normalizeLicense('Apache 2.0')).toBe('Apache-2.0');
    expect(normalizeLicense('MIT OR Apache 2.0')).toBe('MIT OR Apache-2.0');
    expect(normalizeLicense('')).toBe('NOASSERTION');
    expect(normalizeLicenseAllowlist(['mit', 'Apache 2.0', 'MIT'])).toEqual(['MIT', 'Apache-2.0']);
  });

  it('aceita licenças permitidas e rejeita as demais', () => {
    expect(validateLicense('MIT').ok).toBe(true);
    expect(validateLicense('Apache-2.0').ok).toBe(true);
    expect(validateLicense('GPL-3.0').ok).toBe(false);
    expect(validateLicense('NOASSERTION').ok).toBe(false);
    expect(validateLicense('MIT OR GPL-3.0').ok).toBe(false);
    expect(validateLicense('MIT OR Apache-2.0').ok).toBe(true);
    expect(validateLicense('GPL-3.0', ['GPL-3.0']).ok).toBe(true);
    expect(DEFAULT_LICENSE_ALLOWLIST).toContain('MIT');
  });
});

describe('Entitlement - licença offline Ed25519', () => {
  it('verifica uma claim assinada e aplica a política do asset', async () => {
    const keys = await subtle.generateKey({ name: 'Ed25519' }, true, ['sign', 'verify']) as CryptoKeyPair;
    const publicKeyPem = toPublicPem(await subtle.exportKey('spki', keys.publicKey));
    const claim: ILibraryEntitlementClaim = {
      productId: 'arestrix-agents',
      subject: 'empresa-teste',
      tier: 'pro',
      assetIds: ['asset-1'],
      issuedAt: '2026-01-01T00:00:00.000Z',
      expiresAt: '2030-01-01T00:00:00.000Z'
    };
    const entitlement = await signedEntitlement(claim, keys.privateKey);

    const check = await verifyEntitlement(entitlement, publicKeyPem);
    expect(check.valid).toBe(true);
    expect(check.claim?.subject).toBe('empresa-teste');

    const allowed = evaluateAssetEntitlement({
      id: 'asset-1',
      access: { requiresEntitlement: true, productId: 'arestrix-agents', tier: 'pro' }
    }, check);
    expect(allowed.allowed).toBe(true);

    const wrongProduct = evaluateAssetEntitlement({
      id: 'asset-1',
      access: { requiresEntitlement: true, productId: 'outro-produto' }
    }, check);
    expect(wrongProduct.allowed).toBe(false);

    const freeAsset = evaluateAssetEntitlement({ id: 'asset-livre' }, check);
    expect(freeAsset.allowed).toBe(true);

    const tampered: ILibrarySignedEntitlement = {
      ...entitlement,
      claim: { ...claim, tier: 'enterprise' }
    };
    const tamperedCheck = await verifyEntitlement(tampered, publicKeyPem);
    expect(tamperedCheck.valid).toBe(false);
  });

  it('rejeita claim assinada expirada e chave ausente', async () => {
    const keys = await subtle.generateKey({ name: 'Ed25519' }, true, ['sign', 'verify']) as CryptoKeyPair;
    const publicKeyPem = toPublicPem(await subtle.exportKey('spki', keys.publicKey));
    const claim: ILibraryEntitlementClaim = {
      productId: 'arestrix-agents',
      subject: 'empresa-teste',
      tier: 'pro',
      issuedAt: '2020-01-01T00:00:00.000Z',
      expiresAt: '2021-01-01T00:00:00.000Z'
    };
    const entitlement = await signedEntitlement(claim, keys.privateKey);

    expect((await verifyEntitlement(entitlement, publicKeyPem)).valid).toBe(false);
    expect((await verifyEntitlement(entitlement, '')).valid).toBe(false);
  });
});
