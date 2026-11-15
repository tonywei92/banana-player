import base64,re,sys,pathlib
R=pathlib.Path(sys.argv[1]);S=R/'src'
rd=lambda n:(S/n).read_text()
body=re.sub(r'\{\{img:([\w-]+)\}\}',lambda m:'data:image/png;base64,'+base64.b64encode((R/'img'/(m.group(1)+'.png')).read_bytes()).decode(),rd('body.html'))
html=f'''<!doctype html>
<html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Banana Player · handheld redesign</title>
<link rel="preconnect" href="https://fonts.googleapis.com"><link rel="preconnect" href="https://fonts.gstatic.com" crossorigin>
<link href="https://fonts.googleapis.com/css2?family=Inter:wght@400;500;600;700&family=Lora:ital,wght@0,400;0,600;1,400&family=Pixelify+Sans:wght@400;500;600&display=swap" rel="stylesheet">
<style>
{rd('style.css')}
</style></head><body>
{body}
<script>
{rd('kit.js')}
{rd('screens.js')}
{rd('comps.js')}
{rd('app.js')}
</script></body></html>'''
(R/'preview.html').write_text(html);print(len(html))
