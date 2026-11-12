# Entitlements offline — formato e verificação

> **Status:** fundação implementada em 2026-11-12. Não há enforcement automático:
> assets sem `access.requiresEntitlement` continuam livres. A ativação comercial
> (emissão de chaves, canal de distribuição e política de bloqueio) fica para a
> decisão de produto.

## 1. Política no asset

```ts
interface ILibraryAccessPolicy {
  requiresEntitlement?: boolean;
  productId?: string;
  tier?: 'free' | 'pro' | 'enterprise';
}
```

O campo fica em `ILibraryAsset.access`. Ausente ou `requiresEntitlement: false`
significa uso livre. Quando exigido, a claim precisa cobrir:

- o `productId`, se declarado;
- o `tier` mínimo, se declarado;
- o `assetId`, quando a claim lista `assetIds`;
- a data de expiração.

## 2. Claim assinada

```ts
interface ILibrarySignedEntitlement {
  format: 'arestrix-entitlement';
  version: 1;
  claim: {
    productId: string;
    subject: string;
    tier: 'free' | 'pro' | 'enterprise';
    assetIds?: string[];
    seats?: number;
    issuedAt: string;
    expiresAt?: string;
    features?: string[];
  };
  signature: string; // base64url
}
```

A assinatura é **Ed25519** sobre o UTF-8 do **JSON canônico** da claim. O JSON
canônico ordena chaves de objetos recursivamente e preserva a ordem de arrays.
A chave pública é um PEM SPKI fornecido pela instalação ou pela organização.

## 3. Verificação

```ts
const decision = await LibraryService.getInstance().checkAssetEntitlement(
  assetId,
  signedEntitlement,
  publicKeyPem
);
```

O fluxo:

1. valida formato, versão e campos obrigatórios da claim;
2. importa a chave pública via WebCrypto;
3. verifica a assinatura sobre o JSON canônico;
4. rejeita claim expirada;
5. aplica produto, tier e escopo por asset.

Assets livres retornam `allowed: true` sem exigir chave ou entitlement.

## 4. Emissão (fora desta fundação)

O emissor deve:

1. montar a claim;
2. serializar com `canonicalJson(claim)`;
3. assinar com a chave privada Ed25519;
4. codificar a assinatura em base64url;
5. distribuir o JSON assinado ao cliente.

A chave privada nunca entra no repositório, no workspace ou no pacote do
aplicativo. A rotação de chaves exige uma nova chave pública na instalação e um
período de sobreposição para entitlements antigos.
