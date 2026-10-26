/**
 * Ícones vetoriais da Ribbon.
 *
 * A partir da etapa 1 do refresh visual (aba Home), os comandos deixam de usar
 * emojis e passam a usar um conjunto único de SVGs de traço. O viewBox 20x20 e
 * o traço de 1,5 px seguem a mesma escala em todos os tamanhos, evitando o
 * aspecto "misto" de emojis com fontes diferentes no WebView.
 */
export type RibbonIconName =
  | 'agent'
  | 'board'
  | 'bolt'
  | 'book'
  | 'box'
  | 'brain'
  | 'branch'
  | 'bulb'
  | 'catalog'
  | 'chat'
  | 'circle'
  | 'clipboard'
  | 'clock'
  | 'code'
  | 'copy'
  | 'database'
  | 'dice'
  | 'download'
  | 'enter'
  | 'eye'
  | 'file'
  | 'fit'
  | 'folder'
  | 'folderOpen'
  | 'git'
  | 'globe'
  | 'hub'
  | 'image'
  | 'input'
  | 'key'
  | 'keyboard'
  | 'layout'
  | 'list'
  | 'lock'
  | 'orbit'
  | 'output'
  | 'pencil'
  | 'phone'
  | 'play'
  | 'plug'
  | 'plus'
  | 'redo'
  | 'rocket'
  | 'save'
  | 'search'
  | 'settings'
  | 'sheet'
  | 'shield'
  | 'spark'
  | 'stop'
  | 'subflow'
  | 'switch'
  | 'tag'
  | 'target'
  | 'task'
  | 'test'
  | 'thermometer'
  | 'trash'
  | 'undo'
  | 'upload'
  | 'users'
  | 'validate'
  | 'wrench';

const ICONS: Record<RibbonIconName, string> = {
  agent: `
    <rect x="4" y="6.5" width="12" height="9" rx="2"/>
    <path d="M10 6.5V3.5"/>
    <circle cx="10" cy="3" r="1"/>
    <circle cx="7.5" cy="10.5" r=".9" fill="currentColor" stroke="none"/>
    <circle cx="12.5" cy="10.5" r=".9" fill="currentColor" stroke="none"/>
    <path d="M7.5 13.2h5"/>
  `,
  board: `
    <rect x="3" y="4" width="14" height="12" rx="2"/>
    <path d="M6.5 8h7M6.5 11h7M6.5 14h4"/>
  `,
  bolt: `
    <path d="M11 2.5 5 11h4l-1 6.5 6-9h-4l1-6Z"/>
  `,
  book: `
    <path d="M4 4.5A1.5 1.5 0 0 1 5.5 3H16v14H5.5A1.5 1.5 0 0 0 4 18.5Z"/>
    <path d="M4 4.5v14M7 6.5h6M7 9.5h6"/>
  `,
  box: `
    <path d="m10 2.5 7 3.5v8l-7 3.5-7-3.5v-8Z"/>
    <path d="m3 6 7 3.5L17 6M10 9.5v8"/>
  `,
  brain: `
    <path d="M10 3.5a3 3 0 0 0-3 3 3 3 0 0 0-2 5.2 3 3 0 0 0 2 4.8 3 3 0 0 0 3-3V3.5Z"/>
    <path d="M10 3.5a3 3 0 0 1 3 3 3 3 0 0 1 2 5.2 3 3 0 0 1-2 4.8 3 3 0 0 1-3-3V3.5Z"/>
  `,
  branch: `
    <circle cx="4.5" cy="5" r="1.5"/>
    <circle cx="15.5" cy="5" r="1.5"/>
    <circle cx="10" cy="15" r="1.5"/>
    <path d="M4.5 6.5v2A2.5 2.5 0 0 0 7 11h6a2.5 2.5 0 0 0 2.5-2.5v-2M10 11v2.5"/>
  `,
  bulb: `
    <path d="M7 13.5A4.5 4.5 0 1 1 13 13.5c-.7.6-1 1.2-1 2H8c0-.8-.3-1.4-1-2Z"/>
    <path d="M8 17h4M8.5 15.5h3"/>
  `,
  catalog: `
    <rect x="3" y="3" width="14" height="14" rx="2"/>
    <path d="M3 7.5h14M7.5 7.5V17M3 12.5h14"/>
  `,
  file: `
    <path d="M5 2.5h6l4 4v11H5Z"/>
    <path d="M11 2.5v4h4"/>
  `,
  fit: `
    <path d="M7 3H3v4M13 3h4v4M7 17H3v-4M13 17h4v-4"/>
    <circle cx="10" cy="10" r="1.2" fill="currentColor" stroke="none"/>
  `,
  chat: `
    <path d="M3.5 5.5A2.5 2.5 0 0 1 6 3h8a2.5 2.5 0 0 1 2.5 2.5v5A2.5 2.5 0 0 1 14 13H9l-4 4v-4H6a2.5 2.5 0 0 1-2.5-2.5Z"/>
    <path d="M7 7.5h6M7 10h4"/>
  `,
  circle: `
    <circle cx="10" cy="10" r="6.5" fill="currentColor" stroke="none"/>
  `,
  clipboard: `
    <rect x="5" y="4" width="10" height="13" rx="1.5"/>
    <path d="M8 4V2.8h4V4M7.5 8h5M7.5 11h5M7.5 14h3"/>
  `,
  clock: `
    <circle cx="10" cy="10" r="7"/>
    <path d="M10 5.5V10l3 2"/>
  `,
  code: `
    <path d="m7 6-4 4 4 4M13 6l4 4-4 4M11.5 4l-3 12"/>
  `,
  copy: `
    <rect x="7" y="7" width="9" height="9" rx="1.5"/>
    <path d="M13 7V5.5A1.5 1.5 0 0 0 11.5 4h-6A1.5 1.5 0 0 0 4 5.5v6A1.5 1.5 0 0 0 5.5 13H7"/>
  `,
  database: `
    <ellipse cx="10" cy="5" rx="6.5" ry="2.5"/>
    <path d="M3.5 5v10c0 1.4 2.9 2.5 6.5 2.5s6.5-1.1 6.5-2.5V5M3.5 10c0 1.4 2.9 2.5 6.5 2.5s6.5-1.1 6.5-2.5"/>
  `,
  dice: `
    <rect x="3.5" y="3.5" width="13" height="13" rx="2"/>
    <circle cx="7" cy="7" r=".8" fill="currentColor" stroke="none"/>
    <circle cx="10" cy="10" r=".8" fill="currentColor" stroke="none"/>
    <circle cx="13" cy="13" r=".8" fill="currentColor" stroke="none"/>
    <circle cx="13" cy="7" r=".8" fill="currentColor" stroke="none"/>
    <circle cx="7" cy="13" r=".8" fill="currentColor" stroke="none"/>
  `,
  download: `
    <path d="M10 3v10M6 9l4 4 4-4M4 16h12"/>
  `,
  enter: `
    <path d="M4 4v6a2 2 0 0 0 2 2h9M12 9l3 3-3 3"/>
  `,
  eye: `
    <path d="M2.5 10s2.5-5 7.5-5 7.5 5 7.5 5-2.5 5-7.5 5-7.5-5-7.5-5Z"/>
    <circle cx="10" cy="10" r="2.2"/>
  `,
  folder: `
    <path d="M3 6.5A1.5 1.5 0 0 1 4.5 5H8l1.5 2h6A1.5 1.5 0 0 1 17 8.5v6A1.5 1.5 0 0 1 15.5 16h-11A1.5 1.5 0 0 1 3 14.5Z"/>
  `,
  folderOpen: `
    <path d="M3 6.5A1.5 1.5 0 0 1 4.5 5H8l1.5 2H15a1.5 1.5 0 0 1 1.5 1.5v.5"/>
    <path d="M3.5 9.5h14l-1.5 6H5Z"/>
  `,
  git: `
    <circle cx="6" cy="5" r="2"/>
    <circle cx="14" cy="15" r="2"/>
    <circle cx="6" cy="15" r="2"/>
    <path d="M6 7v6M6 10.5h4.5A3.5 3.5 0 0 1 14 14v-1"/>
  `,
  globe: `
    <circle cx="10" cy="10" r="7"/>
    <path d="M3 10h14M10 3c2.2 2 3.2 4.4 3.2 7S12.2 15 10 17M10 3C7.8 5 6.8 7.4 6.8 10S7.8 15 10 17"/>
  `,
  hub: `
    <circle cx="10" cy="10" r="2.2"/>
    <circle cx="4" cy="5" r="1.8"/>
    <circle cx="16" cy="5" r="1.8"/>
    <circle cx="4" cy="15" r="1.8"/>
    <circle cx="16" cy="15" r="1.8"/>
    <path d="m5.5 6.2 2.8 2.3M14.5 6.2l-2.8 2.3M5.5 13.8l2.8-2.3M14.5 13.8l-2.8-2.3"/>
  `,
  image: `
    <rect x="3" y="4" width="14" height="12" rx="2"/>
    <circle cx="7" cy="8" r="1.2"/>
    <path d="m4 15 4-4 3 3 2-2 4 4"/>
  `,
  input: `
    <path d="M4 4h5M4 4v12h5M11 10h6M14 7l3 3-3 3"/>
  `,
  key: `
    <circle cx="7" cy="10" r="3"/>
    <path d="M10 10h7M14 10v2.5M16.5 10v2"/>
  `,
  keyboard: `
    <rect x="2.5" y="6" width="15" height="8" rx="1.5"/>
    <path d="M5 9h.01M8 9h.01M11 9h.01M14 9h.01M5 12h10"/>
  `,
  list: `
    <path d="M4 5h.01M4 10h.01M4 15h.01M7 5h9M7 10h9M7 15h9"/>
  `,
  lock: `
    <rect x="4.5" y="9" width="11" height="8" rx="1.5"/>
    <path d="M7 9V6.5a3 3 0 0 1 6 0V9M10 12v2"/>
  `,
  layout: `
    <rect x="3" y="3" width="5" height="4" rx="1"/>
    <rect x="12" y="3" width="5" height="4" rx="1"/>
    <rect x="7.5" y="13" width="5" height="4" rx="1"/>
    <path d="M5.5 7v3h9V7M10 10v3"/>
  `,
  orbit: `
    <circle cx="10" cy="10" r="2.2"/>
    <ellipse cx="10" cy="10" rx="8" ry="3.2" transform="rotate(-30 10 10)"/>
    <circle cx="15.5" cy="5" r="1" fill="currentColor" stroke="none"/>
  `,
  output: `
    <path d="M15 4h-5M15 4v12h-5M9 10H3M6 7l-3 3 3 3"/>
  `,
  pencil: `
    <path d="m4 16 1-4 8.5-8.5a2.1 2.1 0 0 1 3 3L8 15l-4 1Z"/>
    <path d="m12 5 3 3"/>
  `,
  phone: `
    <path d="M5.5 3.5h3l1 3-1.5 1.2a10 10 0 0 0 4.8 4.8L14 11l3 1v3a1.5 1.5 0 0 1-1.7 1.5A13.5 13.5 0 0 1 4 5.2 1.5 1.5 0 0 1 5.5 3.5Z"/>
  `,
  play: `
    <path d="M7 4.5 16 10l-9 5.5Z" fill="currentColor" stroke="currentColor" stroke-linejoin="round"/>
  `,
  plug: `
    <path d="M7 3v4M13 3v4M5.5 7h9v3a4.5 4.5 0 0 1-4.5 4.5A4.5 4.5 0 0 1 5.5 10V7ZM10 14.5V18"/>
  `,
  plus: `
    <path d="M10 4v12M4 10h12"/>
  `,
  redo: `
    <path d="m12 5 4 4-4 4"/>
    <path d="M16 9H8.5a4.5 4.5 0 0 0 0 9H11"/>
  `,
  rocket: `
    <path d="M10 2.5c2.5 2 4 4.8 4 8l-2 2H8l-2-2c0-3.2 1.5-6 4-8Z"/>
    <circle cx="10" cy="8" r="1.3"/>
    <path d="m6 12-2 4 3-1M14 12l2 4-3-1M8 16h4"/>
  `,
  save: `
    <path d="M4 3h9l3 3v11H4Z"/>
    <path d="M7 3v5h6V3M7 17v-5h6v5"/>
  `,
  search: `
    <circle cx="9" cy="9" r="5"/>
    <path d="m13 13 4 4"/>
  `,
  settings: `
    <path d="M3 6h8M14 6h3M3 14h3M9 14h8"/>
    <circle cx="12.5" cy="6" r="1.8"/>
    <circle cx="7.5" cy="14" r="1.8"/>
  `,
  sheet: `
    <rect x="3" y="3" width="14" height="14" rx="1.5"/>
    <path d="M3 7h14M7.5 7v10M3 12h14"/>
  `,
  shield: `
    <path d="M10 2.5 16 5v5c0 3.2-2.2 5.6-6 7.5-3.8-1.9-6-4.3-6-7.5V5Z"/>
    <path d="m7.2 10 1.8 1.8 3.8-4"/>
  `,
  spark: `
    <path d="M10 2.5 11.5 7 16 8.5 11.5 10 10 14.5 8.5 10 4 8.5 8.5 7Z"/>
    <path d="M15 14v3M13.5 15.5h3"/>
  `,
  subflow: `
    <rect x="2.5" y="3.5" width="9" height="7" rx="1.5"/>
    <rect x="8.5" y="9.5" width="9" height="7" rx="1.5"/>
    <path d="M7.5 10.5v2h3"/>
  `,
  switch: `
    <rect x="2.5" y="6" width="15" height="8" rx="4"/>
    <circle cx="13.5" cy="10" r="2.5" fill="currentColor" stroke="none"/>
  `,
  stop: `
    <rect x="6" y="6" width="8" height="8" rx="1" fill="currentColor" stroke="none"/>
  `,
  tag: `
    <path d="M3 4.5V10l6 6 7-7-6-6H4.5A1.5 1.5 0 0 0 3 4.5Z"/>
    <circle cx="6.5" cy="7" r="1"/>
  `,
  target: `
    <circle cx="10" cy="10" r="7"/>
    <circle cx="10" cy="10" r="3.5"/>
    <circle cx="10" cy="10" r="1" fill="currentColor" stroke="none"/>
  `,
  task: `
    <path d="M11 2.5 5 11h4l-1 6.5 6-9h-4l1-6Z"/>
  `,
  test: `
    <path d="M8 3h4M9 3v5l-4.5 7A1.5 1.5 0 0 0 6 17h8a1.5 1.5 0 0 0 1.5-2L11 8V3"/>
    <path d="M7 13h6"/>
  `,
  thermometer: `
    <path d="M8 11.5V5a2 2 0 0 1 4 0v6.5a3.5 3.5 0 1 1-4 0Z"/>
    <path d="M10 8v6"/>
  `,
  trash: `
    <path d="M4.5 6h11M8 3.5h4l.8 2.5H7.2L8 3.5Z"/>
    <path d="M6.5 6l.7 10.5h5.6L13.5 6"/>
    <path d="M9 9v4.5M11 9v4.5"/>
  `,
  undo: `
    <path d="M8 5 4 9l4 4"/>
    <path d="M4 9h7.5a4.5 4.5 0 0 1 0 9H9"/>
  `,
  upload: `
    <path d="M10 14V4M6 8l4-4 4 4M4 16h12"/>
  `,
  users: `
    <circle cx="7" cy="8" r="2.2"/>
    <circle cx="13.5" cy="8.5" r="1.8"/>
    <path d="M3 16c0-2.2 1.8-4 4-4s4 1.8 4 4M11 16c0-1.7 1.1-3 2.7-3 1.6 0 2.8 1.3 2.8 3"/>
  `,
  validate: `
    <circle cx="10" cy="10" r="7"/>
    <path d="m6.5 10.2 2.3 2.3 4.8-5.2"/>
  `,
  wrench: `
    <path d="M13.5 3.5a3.8 3.8 0 0 0-4.9 4.4L3.5 13l3.5 3.5 5.1-5.1a3.8 3.8 0 0 0 4.4-4.9l-2.4 2.4-2.6-.6-.6-2.6Z"/>
  `
};

export const RIBBON_ICON_NAMES = Object.keys(ICONS) as RibbonIconName[];

/**
 * Retorna o SVG completo de um ícone da Ribbon. O `className` é opcional e
 * permite aplicar cores de destaque sem duplicar o markup.
 */
export function ribbonIcon(
  name: RibbonIconName,
  size: number = 20,
  className: string = ''
): string {
  const classes = ['ribbon-glyph', className].filter(Boolean).join(' ');
  return (
    `<svg class="${classes}" width="${size}" height="${size}" viewBox="0 0 20 20" ` +
    `fill="none" stroke="currentColor" stroke-width="1.5" stroke-linecap="round" ` +
    `stroke-linejoin="round" aria-hidden="true" focusable="false">${ICONS[name]}</svg>`
  );
}
