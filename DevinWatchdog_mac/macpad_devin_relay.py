import asyncio, iterm2, sys
SID='EE473D24-793C-48E3-B8A9-2298BB40327F'
async def main(conn):
 app=await iterm2.async_get_app(conn)
 s=app.get_session_by_id(SID)
 if not s: raise RuntimeError('Target session missing')
 tty=await s.async_get_variable('tty')
 name=await s.async_get_variable('name')
 if tty != '/dev/ttys001' or 'macPad' not in (name or s.name): raise RuntimeError((tty,name,s.name))
 screen=await s.async_get_screen_contents()
 lines='\n'.join(screen.line(i).string for i in range(screen.number_of_lines))
 print('SESSION',SID,tty,name or s.name)
 if sys.argv[1]=='read': print(lines); return
 if sys.argv[1]=='flush':
  if 'Press Enter to send queued messages now' not in lines: raise RuntimeError('No verified queued-message prompt')
  await s.async_send_text('\r',suppress_broadcast=True)
  print('Delivered already queued guidance'); return
 if sys.argv[1]!='guide' and ('Ask Devin' not in lines or 'esc twice to interrupt' in lines): raise RuntimeError('Not verified idle; nothing sent')
 if sys.argv[1]=='guide' and 'Guide Devin while it works' not in lines: raise RuntimeError('Not verified working; use send after reading state')
 if len(sys.argv)!=3 or sys.argv[1] not in ('send','guide'): raise RuntimeError('Usage: read | send MESSAGE_FILE | guide MESSAGE_FILE')
 from pathlib import Path
 msg=Path(sys.argv[2]).read_text().strip()
 if not msg or len(msg)>12000: raise RuntimeError('Message empty or too long')
 await s.async_send_text(msg,suppress_broadcast=True)
 await asyncio.sleep(.7)
 await s.async_send_text('\r',suppress_broadcast=True)
 print('SENT message file; body omitted')
iterm2.run_until_complete(main)
