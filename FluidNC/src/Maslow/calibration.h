
void printIndividual(double individual[]);
void printPopulation(double population[][6]);
void assign(int index, double population[][6], double individual[]);
void mutate(int index, double population[][6],double maxMutation);
double findPointClosestToArcsDist(double measurement[], double individual[]);
void evaluateFitness(double individual[], double measurements[][4]);
void computeCalibration(double measurements[][4], double result[6], void (*webPrint) (double arg1),double tlX,double tlY, double trX, double trY, double brX, double tlZ, double trZ, double blZ, double brZ);
double myAbs(double x);
