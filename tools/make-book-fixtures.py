#!/usr/bin/env python3
# tests/fixtures/books: a small CBR (RAR 4, stored) and a two-page PDF for core-test's PageBook test. Written by hand
# (no RAR encoder is free to use), so the test needs no tool: run again only to change them.
import struct, zlib, os, sys
out = os.path.join(os.path.dirname(__file__), '..', 'tests', 'fixtures', 'books')
os.makedirs(out, exist_ok=True)

def png(w, h, rgb):
	raw = b''.join(b'\0' + bytes(rgb) * w for _ in range(h))
	def chunk(t, d): return struct.pack('>I', len(d)) + t + d + struct.pack('>I', zlib.crc32(t + d))
	return b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0)) + chunk(b'IDAT', zlib.compress(raw)) + chunk(b'IEND', b'')

def rar4(files):
	def head(t, flags, body):
		h = struct.pack('<BHH', t, flags, 7 + len(body)) + body
		return struct.pack('<H', zlib.crc32(h) & 0xffff) + h
	data = b'Rar!\x1a\x07\x00' + head(0x73, 0, b'\0' * 6)
	for name, blob in files:
		n = name.encode()
		body = struct.pack('<IIBIIBBHI', len(blob), len(blob), 2, zlib.crc32(blob), 0x5A000000, 20, 0x30, len(n), 0x20) + n
		data += head(0x74, 0x8000, body) + blob
	return data + head(0x7b, 0x4000, b'')

info = b'<?xml version="1.0"?><ComicInfo><Series>Fixture</Series><Number>7</Number></ComicInfo>'
# out of order in the archive, so the test sees the pages sorted
open(os.path.join(out, 'chapter.cbr'), 'wb').write(rar4([
	('pages/10.png', png(4, 6, (0, 0, 255))), ('ComicInfo.xml', info),
	('pages/2.png', png(4, 6, (0, 255, 0))), ('pages/1.png', png(4, 6, (255, 0, 0))),
	('__MACOSX/._1.png', b'junk')]))

def pdf(pages):
	objs = ['<< /Type /Catalog /Pages 2 0 R >>', None]
	kids = []
	for i, (w, h, col) in enumerate(pages):
		content = f'{col[0]} {col[1]} {col[2]} rg 0 0 {w} {h} re f'.encode()
		pi = len(objs) + 1
		objs.append(f'<< /Type /Page /Parent 2 0 R /MediaBox [0 0 {w} {h}] /Contents {pi + 1} 0 R >>')
		objs.append(f'<< /Length {len(content)} >>\nstream\n{content.decode()}\nendstream')
		kids.append(f'{pi} 0 R')
	objs[1] = f'<< /Type /Pages /Kids [{" ".join(kids)}] /Count {len(pages)} >>'
	b = b'%PDF-1.4\n'
	offs = []
	for i, o in enumerate(objs):
		offs.append(len(b))
		b += f'{i + 1} 0 obj\n{o}\nendobj\n'.encode()
	x = len(b)
	b += f'xref\n0 {len(objs) + 1}\n0000000000 65535 f \n'.encode() + b''.join(f'{o:010d} 00000 n \n'.encode() for o in offs)
	b += f'trailer\n<< /Size {len(objs) + 1} /Root 1 0 R >>\nstartxref\n{x}\n%%EOF\n'.encode()
	return b
# a red portrait page and a green landscape one
open(os.path.join(out, 'two.pdf'), 'wb').write(pdf([(200, 300, (1, 0, 0)), (300, 200, (0, 1, 0))]))

# EPUBs: a novel (two chapters, a nav, a picture) and a comic (a fixed layout, a picture a page)
import zipfile
def epub(name, opf_extra, manifest, spine, files, nav=None):
	with zipfile.ZipFile(os.path.join(out, name), 'w') as z:
		z.writestr(zipfile.ZipInfo('mimetype'), 'application/epub+zip')
		z.writestr('META-INF/container.xml', '<?xml version="1.0"?><container version="1.0" xmlns="urn:oasis:names:tc:opendocument:xmlns:container"><rootfiles><rootfile full-path="OEBPS/content.opf" media-type="application/oebps-package+xml"/></rootfiles></container>')
		items = ''.join(f'<item id="{i}" href="{h}" media-type="{t}"{p}/>' for i, h, t, p in manifest)
		refs = ''.join(f'<itemref idref="{i}"/>' for i in spine)
		z.writestr('OEBPS/content.opf', f'<?xml version="1.0"?><package xmlns="http://www.idpf.org/2007/opf" version="3.0"><metadata xmlns:dc="http://purl.org/dc/elements/1.1/"><dc:title>Fixture &amp; Book</dc:title>{opf_extra}</metadata><manifest>{items}</manifest><spine>{refs}</spine></package>')
		for n, data in files.items(): z.writestr('OEBPS/' + n, data)

xh = lambda body: f'<?xml version="1.0" encoding="utf-8"?><html xmlns="http://www.w3.org/1999/xhtml" xmlns:epub="http://www.idpf.org/2007/ops"><head><title>t</title><style>p{{x:y}}</style></head><body>{body}</body></html>'
epub('novel.epub', '', [
	('nav', 'nav.xhtml', 'application/xhtml+xml', ' properties="nav"'), ('c1', 'Text/c1.xhtml', 'application/xhtml+xml', ''),
	('c2', 'Text/c%202.xhtml', 'application/xhtml+xml', ''), ('img', 'Images/p.png', 'image/png', '')],
	['c1', 'c2'], {
	'nav.xhtml': xh('<nav epub:type="toc"><ol><li><a href="Text/c1.xhtml">One</a><ol><li><a href="Text/c1.xhtml#s">One A</a></li></ol></li><li><a href="Text/c%202.xhtml">Two</a></li></ol></nav>'),
	'Text/c1.xhtml': xh('<h1>Chapter  One</h1><p>It was a <em>dark</em> and <b>stormy</b>\n night.</p><div><p>Inner</p>tail</div><p>a<br/>b</p><hr/><img src="../Images/p.png" alt="pic"/><ul><li>item</li></ul>'),
	'Text/c 2.xhtml': xh('<h2>Two</h2><blockquote><p>Quoted</p></blockquote><pre>line 1\n  line 2</pre>'),
	'Images/p.png': png(4, 6, (255, 0, 0))})
epub('comic.epub', '<meta property="rendition:layout">pre-paginated</meta>', [
	('p1', 'p1.xhtml', 'application/xhtml+xml', ''), ('p2', 'p2.xhtml', 'application/xhtml+xml', ''),
	('i1', 'i/1.png', 'image/png', ''), ('i2', 'i/2.png', 'image/png', '')],
	['p1', 'p2'], {
	'p1.xhtml': xh('<div><img src="i/1.png"/></div>'),
	'p2.xhtml': xh('<svg xmlns="http://www.w3.org/2000/svg" xmlns:xlink="http://www.w3.org/1999/xlink"><image xlink:href="i/2.png"/></svg>'),
	'i/1.png': png(4, 6, (0, 255, 0)), 'i/2.png': png(4, 6, (0, 0, 255))})
