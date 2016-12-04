functor

import
  Apache at 'x-oz://boot/Apache'
  Open
define
  {Apache.setContentType 'Content-Type: text/html; charset=UTF-8'}
  {Apache.rputs "<!doctype html><html><body>"}
  {Apache.rflush}
  local
    fun {FromLittleEndian Buf}
      case Buf of nil then 0
      [] H|T then
        H + {FromLittleEndian T} * 256
      end
    end
    proc {ReadAndPut}
      local H Count
      in
        try 
          {Conn read(size:4 list:?H tail:nil len:?Count)}
        catch _ then
          Count = 0
        end 
        if Count == 4 then
          {Apache.rputs [{FromLittleEndian H}]}
          {Apache.rflush}
          {ReadAndPut}
        end
      end
    end
    Conn = {New Open.socket init(type:'stream' protocol:'tcp')}
  in
    {Conn connect(host:'127.0.0.1' port:20408)}
    {ReadAndPut}
  end
  {Apache.rputs "</body></html>"}
end
