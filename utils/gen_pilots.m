scrambler = [0,0,0,0,1,1,1,0,1,1,1,1,0,0,1,0,1,1,0,0,1,0,0,1,0,0,0,0,0,0,1,0,0,0,1,0,0,1,1,0,0,0,1,0,1,1,1,0,1,0,1,1,0,1,1,0,0,0,0,0,1,1,0,0,1,1,0,1,0,1,0,0,1,1,1,0,0,1,1,1,1,0,1,1,0,1,0,0,0,0,1,0,1,0,1,0,1,1,1,1,1,0,1,0,0,1,0,1,0,0,0,1,1,0,1,1,1,0,0,0,1,1,1,1,1,1,1];
scrambler = - (scrambler * 2 - 1);
MAX_PSDU_SIZE = 511;
max_symbols = ((8 * MAX_PSDU_SIZE + 8 + 6) / 6);

fprintf("(")
for i=1:max_symbols
  
  if mod(i-1,2) == 0
    mapping = 1;
  else
    mapping = -1;
  endif
  
  fprintf("(");
  fprintf('%d', scrambler(mod(i-1,length(scrambler)) + 1) * mapping);
  fprintf(", ");
  fprintf('%d', - scrambler(mod(i-1,length(scrambler)) + 1) * mapping);
  fprintf("),");
endfor

fprintf(")");
