scrambler = [0,0,0,0,1,1,1,0,1,1,1,1,0,0,1,0,1,1,0,0,1,0,0,1,0,0,0,0,0,0,1,0,0,0,1,0,0,1,1,0,0,0,1,0,1,1,1,0,1,0,1,1,0,1,1,0,0,0,0,0,1,1,0,0,1,1,0,1,0,1,0,0,1,1,1,0,0,1,1,1,1,0,1,1,0,1,0,0,0,0,1,0,1,0,1,0,1,1,1,1,1,0,1,0,0,1,0,1,0,0,0,1,1,0,1,1,1,0,0,0,1,1,1,1,1,1,1];
scrambler = - (scrambler * 2 - 1);

fprintf("(")
for i=1:length(scrambler)
  
  if mod(i-1,2) == 0
    mapping = 1;
  else
    mapping = -1;
  endif
  
  fprintf("(")
  fprintf('%d', scrambler(i) * mapping)
  fprintf(", ")
  fprintf('%d', - scrambler(i) * mapping)
  fprintf("),");
endfor

fprintf(")")
