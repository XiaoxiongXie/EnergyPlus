// EnergyPlus, Copyright (c) 1996-2019, The Board of Trustees of the University of Illinois,
// The Regents of the University of California, through Lawrence Berkeley National Laboratory
// (subject to receipt of any required approvals from the U.S. Dept. of Energy), Oak Ridge
// National Laboratory, managed by UT-Battelle, Alliance for Sustainable Energy, LLC, and other
// contributors. All rights reserved.
//
// NOTICE: This Software was developed under funding from the U.S. Department of Energy and the
// U.S. Government consequently retains certain rights. As such, the U.S. Government has been
// granted for itself and others acting on its behalf a paid-up, nonexclusive, irrevocable,
// worldwide license in the Software to reproduce, distribute copies to the public, prepare
// derivative works, and perform publicly and display publicly, and to permit others to do so.
//
// Redistribution and use in source and binary forms, with or without modification, are permitted
// provided that the following conditions are met:
//
// (1) Redistributions of source code must retain the above copyright notice, this list of
//     conditions and the following disclaimer.
//
// (2) Redistributions in binary form must reproduce the above copyright notice, this list of
//     conditions and the following disclaimer in the documentation and/or other materials
//     provided with the distribution.
//
// (3) Neither the name of the University of California, Lawrence Berkeley National Laboratory,
//     the University of Illinois, U.S. Dept. of Energy nor the names of its contributors may be
//     used to endorse or promote products derived from this software without specific prior
//     written permission.
//
// (4) Use of EnergyPlus(TM) Name. If Licensee (i) distributes the software in stand-alone form
//     without changes from the version obtained under this License, or (ii) Licensee makes a
//     reference solely to the software portion of its product, Licensee must refer to the
//     software as "EnergyPlus version X" software, where "X" is the version number Licensee
//     obtained under this License and may not use a different name for the software. Except as
//     specifically required in this Section (4), Licensee shall not use in a company name, a
//     product name, in advertising, publicity, or other promotional activities any name, trade
//     name, trademark, logo, or other designation of "EnergyPlus", "E+", "e+" or confusingly
//     similar designation, without the U.S. Department of Energy's prior written consent.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR
// IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY
// AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
// CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
// SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
// OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.

// C++ Headers
#include <cassert>
#include <cmath>

// ObjexxFCL Headers
#include <ObjexxFCL/Array.functions.hh>
#include <ObjexxFCL/Fmath.hh>

// EnergyPlus Headers
#include <Boilers.hh>
#include <BranchNodeConnections.hh>
#include <CurveManager.hh>
#include <DataBranchAirLoopPlant.hh>
#include <DataGlobalConstants.hh>
#include <DataHVACGlobals.hh>
#include <DataIPShortCuts.hh>
#include <DataLoopNode.hh>
#include <DataPlant.hh>
#include <DataPrecisionGlobals.hh>
#include <DataSizing.hh>
#include <EMSManager.hh>
#include <FaultsManager.hh>
#include <FluidProperties.hh>
#include <General.hh>
#include <GlobalNames.hh>
#include <InputProcessing/InputProcessor.hh>
#include <NodeInputManager.hh>
#include <OutputProcessor.hh>
#include <OutputReportPredefined.hh>
#include <PlantUtilities.hh>
#include <ReportSizingManager.hh>
#include <UtilityRoutines.hh>

namespace EnergyPlus {

namespace Boilers {

    // Module containing the routines dealing with the Boilers

    // MODULE INFORMATION:
    //       AUTHOR         Dan Fisher, Taecheol Kim
    //       DATE WRITTEN   1998, 2000
    //       MODIFIED       na
    //       RE-ENGINEERED  na

    // PURPOSE OF THIS MODULE:
    // Perform boiler simulation for plant simulation

    // METHODOLOGY EMPLOYED:
    // The BLAST/DOE-2 empirical model based on mfg. data

    // Boiler normalized efficiency curve types
    int const Linear(1);
    int const BiLinear(2);
    int const Quadratic(3);
    int const BiQuadratic(4);
    int const Cubic(5);
    int const QuadraticLinear(6);
    int const BiCubic(7);

    // water temperature evaluation method
    int const BoilerTempModeNotSet(100);
    int const EnteringBoilerTemp(101);
    int const LeavingBoilerTemp(102);

    // Boiler flow modes
    int const FlowModeNotSet(200);
    int const ConstantFlow(201);
    int const NotModulated(202);
    int const LeavingSetPointModulated(203);

    // MODULE VARIABLE DECLARATIONS:
    int NumBoilers(0);              // Number of boilers
    bool GetBoilerInputFlag(true);
    Array1D_bool CheckEquipName;

    // Object Data
    Array1D<BoilerSpecs> Boiler;      // boiler data - dimension to number of machines

    void clear_state()
    {
        NumBoilers = 0;
        CheckEquipName.deallocate();
        Boiler.deallocate();
        GetBoilerInputFlag = true;
    }

    void SimBoiler(std::string const &EP_UNUSED(BoilerType), // boiler type (used in CASE statement)
                   std::string const &BoilerName,            // boiler identifier
                   int const EquipFlowCtrl,                  // Flow control mode for the equipment
                   int &CompIndex,                           // boiler counter/identifier
                   bool const RunFlag,                       // if TRUE run boiler simulation--boiler is ON
                   bool &InitLoopEquip,                      // If not zero, calculate the max load for operating conditions
                   Real64 &MyLoad,                           // W - Actual demand boiler must satisfy--calculated by load dist. routine
                   Real64 &MaxCap,                           // W - maximum boiler operating capacity
                   Real64 &MinCap,                           // W - minimum boiler operating capacity
                   Real64 &OptCap,                           // W - optimal boiler operating capacity
                   bool const GetSizingFactor,               // TRUE when just the sizing factor is requested
                   Real64 &SizingFactor                      // sizing factor
    )
    {
        // SUBROUTINE INFORMATION:
        //       AUTHOR         DAN FISHER
        //       DATE WRITTEN   Sept. 1998
        //       MODIFIED       Taecheol Kim, May 2000
        //       RE-ENGINEERED  na

        // PURPOSE OF THIS SUBROUTINE:
        // This subroutine controls the boiler component simulation

        int BoilerNum; // boiler counter/identifier

        // Get Input
        if (GetBoilerInputFlag) {
            GetBoilerInput();
            GetBoilerInputFlag = false;
        }

        // Find the correct Equipment
        if (CompIndex == 0) {
            BoilerNum = UtilityRoutines::FindItemInList(BoilerName, Boiler);
            if (BoilerNum == 0) {
                ShowFatalError("SimBoiler: Unit not found=" + BoilerName);
            }
            CompIndex = BoilerNum;
        } else {
            BoilerNum = CompIndex;
            if (BoilerNum > NumBoilers || BoilerNum < 1) {
                ShowFatalError("SimBoiler:  Invalid CompIndex passed=" + General::TrimSigDigits(BoilerNum) + ", Number of Units=" + General::TrimSigDigits(NumBoilers) +
                               ", Entered Unit name=" + BoilerName);
            }
            if (CheckEquipName(BoilerNum)) {
                if (BoilerName != Boiler(BoilerNum).Name) {
                    ShowFatalError("SimBoiler: Invalid CompIndex passed=" + General::TrimSigDigits(BoilerNum) + ", Unit name=" + BoilerName +
                                   ", stored Unit Name for that index=" + Boiler(BoilerNum).Name);
                }
                CheckEquipName(BoilerNum) = false;
            }
        }

        // Initialize Loop Equipment
        if (InitLoopEquip) {
            //			Boiler( BoilerNum ).IsThisSized = false;
            InitBoiler(BoilerNum);
            //			Boiler( BoilerNum ).IsThisSized = true;
            SizeBoiler(BoilerNum);
            MinCap = Boiler(BoilerNum).NomCap * Boiler(BoilerNum).MinPartLoadRat;
            MaxCap = Boiler(BoilerNum).NomCap * Boiler(BoilerNum).MaxPartLoadRat;
            OptCap = Boiler(BoilerNum).NomCap * Boiler(BoilerNum).OptPartLoadRat;
            if (GetSizingFactor) {
                SizingFactor = Boiler(BoilerNum).SizFac;
            }
            return;
        }
        // Calculate Load

        // Select boiler type and call boiler model
        InitBoiler(BoilerNum);
        CalcBoilerModel(BoilerNum, MyLoad, RunFlag, EquipFlowCtrl);
        UpdateBoilerRecords(MyLoad, RunFlag, BoilerNum);
    }

    void GetBoilerInput()
    {
        // SUBROUTINE INFORMATION:
        //       AUTHOR:          Dan Fisher
        //       DATE WRITTEN:    April 1998
        //       MODIFIED:        R. Raustad - FSEC, June 2008: added boiler efficiency curve object
        //       RE-ENGINEERED:   na

        // PURPOSE OF THIS SUBROUTINE:
        // get all boiler data from input file

        // METHODOLOGY EMPLOYED:
        // standard EnergyPlus input retrieval using input Processor

        // Locals
        static std::string const RoutineName("GetBoilerInput: ");

        // LOCAL VARIABLES
        bool ErrorsFound(false);  // Flag to show errors were found during GetInput

        // GET NUMBER OF ALL EQUIPMENT
        DataIPShortCuts::cCurrentModuleObject = "Boiler:HotWater";
        NumBoilers = inputProcessor->getNumObjectsFound(DataIPShortCuts::cCurrentModuleObject);

        if (NumBoilers <= 0) {
            ShowSevereError("No " + DataIPShortCuts::cCurrentModuleObject + " Equipment specified in input file");
            ErrorsFound = true;
        }

        // See if load distribution manager has already gotten the input
        if (allocated(Boiler)) return;

        Boiler.allocate(NumBoilers);
        CheckEquipName.allocate(NumBoilers);
        CheckEquipName = true;

        // LOAD ARRAYS WITH CURVE FIT Boiler DATA

        for (int BoilerNum = 1; BoilerNum <= NumBoilers; ++BoilerNum) {
            int NumAlphas;  // Number of elements in the alpha array
            int NumNums;  // Number of elements in the numeric array
            int IOStat;  // IO Status when calling get input subroutine
            inputProcessor->getObjectItem(DataIPShortCuts::cCurrentModuleObject,
                                          BoilerNum,
                                          DataIPShortCuts::cAlphaArgs,
                                          NumAlphas,
                                          DataIPShortCuts::rNumericArgs,
                                          NumNums,
                                          IOStat,
                                          DataIPShortCuts::lNumericFieldBlanks,
                                          DataIPShortCuts::lAlphaFieldBlanks,
                                          DataIPShortCuts::cAlphaFieldNames,
                                          DataIPShortCuts::cNumericFieldNames);
            UtilityRoutines::IsNameEmpty(DataIPShortCuts::cAlphaArgs(1), DataIPShortCuts::cCurrentModuleObject, ErrorsFound);
            // ErrorsFound will be set to True if problem was found, left untouched otherwise
            GlobalNames::VerifyUniqueBoilerName(DataIPShortCuts::cCurrentModuleObject, DataIPShortCuts::cAlphaArgs(1), ErrorsFound, DataIPShortCuts::cCurrentModuleObject + " Name");
            Boiler(BoilerNum).Name = DataIPShortCuts::cAlphaArgs(1);
            Boiler(BoilerNum).TypeNum = DataPlant::TypeOf_Boiler_Simple;

            {
                auto const SELECT_CASE_var(DataIPShortCuts::cAlphaArgs(2));

                if ((SELECT_CASE_var == "ELECTRICITY") || (SELECT_CASE_var == "ELECTRIC") || (SELECT_CASE_var == "ELEC")) {
                    Boiler(BoilerNum).BoilerFuelTypeForOutputVariable = "Electric";
                    Boiler(BoilerNum).FuelType = DataGlobalConstants::AssignResourceTypeNum("ELECTRICITY");

                } else if ((SELECT_CASE_var == "GAS") || (SELECT_CASE_var == "NATURALGAS") || (SELECT_CASE_var == "NATURAL GAS")) {
                    Boiler(BoilerNum).BoilerFuelTypeForOutputVariable = "Gas";
                    Boiler(BoilerNum).FuelType = DataGlobalConstants::AssignResourceTypeNum("NATURALGAS");

                } else if (SELECT_CASE_var == "DIESEL") {
                    Boiler(BoilerNum).BoilerFuelTypeForOutputVariable = "Diesel";
                    Boiler(BoilerNum).FuelType = DataGlobalConstants::AssignResourceTypeNum("DIESEL");

                } else if (SELECT_CASE_var == "GASOLINE") {
                    Boiler(BoilerNum).BoilerFuelTypeForOutputVariable = "Gasoline";
                    Boiler(BoilerNum).FuelType = DataGlobalConstants::AssignResourceTypeNum("GASOLINE");

                } else if (SELECT_CASE_var == "COAL") {
                    Boiler(BoilerNum).BoilerFuelTypeForOutputVariable = "Coal";
                    Boiler(BoilerNum).FuelType = DataGlobalConstants::AssignResourceTypeNum("COAL");

                } else if ((SELECT_CASE_var == "FUEL OIL #1") || (SELECT_CASE_var == "FUELOIL#1") || (SELECT_CASE_var == "FUEL OIL") ||
                           (SELECT_CASE_var == "DISTILLATE OIL")) {
                    Boiler(BoilerNum).BoilerFuelTypeForOutputVariable = "FuelOil#1";
                    Boiler(BoilerNum).FuelType = DataGlobalConstants::AssignResourceTypeNum("DISTILLATE OIL");

                } else if ((SELECT_CASE_var == "FUEL OIL #2") || (SELECT_CASE_var == "FUELOIL#2") || (SELECT_CASE_var == "RESIDUAL OIL")) {
                    Boiler(BoilerNum).BoilerFuelTypeForOutputVariable = "FuelOil#2";
                    Boiler(BoilerNum).FuelType = DataGlobalConstants::AssignResourceTypeNum("RESIDUAL OIL");

                } else if ((SELECT_CASE_var == "PROPANE") || (SELECT_CASE_var == "LPG") || (SELECT_CASE_var == "PROPANEGAS") ||
                           (SELECT_CASE_var == "PROPANE GAS")) {
                    Boiler(BoilerNum).BoilerFuelTypeForOutputVariable = "Propane";
                    Boiler(BoilerNum).FuelType = DataGlobalConstants::AssignResourceTypeNum("PROPANE");

                } else if (SELECT_CASE_var == "OTHERFUEL1") {
                    Boiler(BoilerNum).BoilerFuelTypeForOutputVariable = "OtherFuel1";
                    Boiler(BoilerNum).FuelType = DataGlobalConstants::AssignResourceTypeNum("OTHERFUEL1");

                } else if (SELECT_CASE_var == "OTHERFUEL2") {
                    Boiler(BoilerNum).BoilerFuelTypeForOutputVariable = "OtherFuel2";
                    Boiler(BoilerNum).FuelType = DataGlobalConstants::AssignResourceTypeNum("OTHERFUEL2");

                } else {
                    ShowSevereError(RoutineName + DataIPShortCuts::cCurrentModuleObject + "=\"" + DataIPShortCuts::cAlphaArgs(1) + "\",");
                    ShowContinueError("Invalid " + DataIPShortCuts::cAlphaFieldNames(2) + '=' + DataIPShortCuts::cAlphaArgs(2));
                    // Set to Electric to avoid errors when setting up output variables
                    Boiler(BoilerNum).BoilerFuelTypeForOutputVariable = "Electric";
                    Boiler(BoilerNum).FuelType = DataGlobalConstants::AssignResourceTypeNum("ELECTRICITY");
                    ErrorsFound = true;
                }
            }

            Boiler(BoilerNum).NomCap = DataIPShortCuts::rNumericArgs(1);
            if (DataIPShortCuts::rNumericArgs(1) == 0.0) {
                ShowSevereError(RoutineName + DataIPShortCuts::cCurrentModuleObject + "=\"" + DataIPShortCuts::cAlphaArgs(1) + "\",");
                ShowContinueError("Invalid " + DataIPShortCuts::cNumericFieldNames(1) + '=' + General::RoundSigDigits(DataIPShortCuts::rNumericArgs(1), 2));
                ShowContinueError("..." + DataIPShortCuts::cNumericFieldNames(1) + " must be greater than 0.0");
                ErrorsFound = true;
            }
            if (Boiler(BoilerNum).NomCap == DataSizing::AutoSize) {
                Boiler(BoilerNum).NomCapWasAutoSized = true;
            }

            Boiler(BoilerNum).Effic = DataIPShortCuts::rNumericArgs(2);
            if (DataIPShortCuts::rNumericArgs(2) == 0.0) {
                ShowSevereError(RoutineName + DataIPShortCuts::cCurrentModuleObject + "=\"" + DataIPShortCuts::cAlphaArgs(1) + "\",");
                ShowContinueError("Invalid " + DataIPShortCuts::cNumericFieldNames(2) + '=' + General::RoundSigDigits(DataIPShortCuts::rNumericArgs(2), 3));
                ShowSevereError("..." + DataIPShortCuts::cNumericFieldNames(2) + " must be greater than 0.0");
                ErrorsFound = true;
            }

            {
                auto const SELECT_CASE_var(DataIPShortCuts::cAlphaArgs(3));

                if (SELECT_CASE_var == "ENTERINGBOILER") {
                    Boiler(BoilerNum).CurveTempMode = EnteringBoilerTemp;
                } else if (SELECT_CASE_var == "LEAVINGBOILER") {
                    Boiler(BoilerNum).CurveTempMode = LeavingBoilerTemp;
                } else {
                    Boiler(BoilerNum).CurveTempMode = BoilerTempModeNotSet;
                }
            }

            Boiler(BoilerNum).EfficiencyCurvePtr = CurveManager::GetCurveIndex(DataIPShortCuts::cAlphaArgs(4));
            if (Boiler(BoilerNum).EfficiencyCurvePtr > 0) {
                ErrorsFound |= CurveManager::CheckCurveDims(
                    Boiler(BoilerNum).EfficiencyCurvePtr,   // Curve index
                    {1, 2},                            // Valid dimensions
                    RoutineName,                    // Routine name
                    DataIPShortCuts::cCurrentModuleObject,            // Object Type
                    Boiler(BoilerNum).Name,         // Object Name
                    DataIPShortCuts::cAlphaFieldNames(4));               // Field Name

                // if curve uses temperature, make sure water temp mode has been set
                if (CurveManager::PerfCurve(Boiler(BoilerNum).EfficiencyCurvePtr).NumDims == 2) {                                // curve uses water temperature
                    if (Boiler(BoilerNum).CurveTempMode == BoilerTempModeNotSet) { // throw error
                        if (!DataIPShortCuts::lAlphaFieldBlanks(3)) {
                            ShowSevereError(RoutineName + DataIPShortCuts::cCurrentModuleObject + "=\"" + DataIPShortCuts::cAlphaArgs(1) + "\",");
                            ShowContinueError("Invalid " + DataIPShortCuts::cAlphaFieldNames(3) + '=' + DataIPShortCuts::cAlphaArgs(3));
                            ShowContinueError("Boiler using curve type of " + CurveManager::PerfCurve(Boiler(BoilerNum).EfficiencyCurvePtr).ObjectType + " must specify " +
                                                  DataIPShortCuts::cAlphaFieldNames(3));
                            ShowContinueError("Available choices are EnteringBoiler or LeavingBoiler");
                        } else {
                            ShowSevereError(RoutineName + DataIPShortCuts::cCurrentModuleObject + "=\"" + DataIPShortCuts::cAlphaArgs(1) + "\",");
                            ShowContinueError("Field " + DataIPShortCuts::cAlphaFieldNames(3) + " is blank");
                            ShowContinueError("Boiler using curve type of " + CurveManager::PerfCurve(Boiler(BoilerNum).EfficiencyCurvePtr).ObjectType +
                                              " must specify either EnteringBoiler or LeavingBoiler");
                        }
                        ErrorsFound = true;
                    }
                }

            } else if (!DataIPShortCuts::lAlphaFieldBlanks(4)) {
                ShowSevereError(RoutineName + DataIPShortCuts::cCurrentModuleObject + "=\"" + DataIPShortCuts::cAlphaArgs(1) + "\",");
                ShowContinueError("Invalid " + DataIPShortCuts::cAlphaFieldNames(4) + '=' + DataIPShortCuts::cAlphaArgs(4));
                ShowSevereError("..." + DataIPShortCuts::cAlphaFieldNames(4) + " not found.");
                ErrorsFound = true;
            }
            Boiler(BoilerNum).VolFlowRate = DataIPShortCuts::rNumericArgs(3);
            if (Boiler(BoilerNum).VolFlowRate == DataSizing::AutoSize) {
                Boiler(BoilerNum).VolFlowRateWasAutoSized = true;
            }
            Boiler(BoilerNum).MinPartLoadRat = DataIPShortCuts::rNumericArgs(4);
            Boiler(BoilerNum).MaxPartLoadRat = DataIPShortCuts::rNumericArgs(5);
            Boiler(BoilerNum).OptPartLoadRat = DataIPShortCuts::rNumericArgs(6);

            Boiler(BoilerNum).TempUpLimitBoilerOut = DataIPShortCuts::rNumericArgs(7);
            // default to 99.9C if upper temperature limit is left blank.
            if (Boiler(BoilerNum).TempUpLimitBoilerOut <= 0.0) {
                Boiler(BoilerNum).TempUpLimitBoilerOut = 99.9;
            }

            Boiler(BoilerNum).ParasiticElecLoad = DataIPShortCuts::rNumericArgs(8);
            Boiler(BoilerNum).SizFac = DataIPShortCuts::rNumericArgs(9);
            if (Boiler(BoilerNum).SizFac == 0.0) Boiler(BoilerNum).SizFac = 1.0;

            Boiler(BoilerNum).BoilerInletNodeNum = NodeInputManager::GetOnlySingleNode(
                DataIPShortCuts::cAlphaArgs(5), ErrorsFound, DataIPShortCuts::cCurrentModuleObject, DataIPShortCuts::cAlphaArgs(1), DataLoopNode::NodeType_Water, DataLoopNode::NodeConnectionType_Inlet, 1, DataLoopNode::ObjectIsNotParent);
            Boiler(BoilerNum).BoilerOutletNodeNum = NodeInputManager::GetOnlySingleNode(
                DataIPShortCuts::cAlphaArgs(6), ErrorsFound, DataIPShortCuts::cCurrentModuleObject, DataIPShortCuts::cAlphaArgs(1), DataLoopNode::NodeType_Water, DataLoopNode::NodeConnectionType_Outlet, 1, DataLoopNode::ObjectIsNotParent);
            BranchNodeConnections::TestCompSet(DataIPShortCuts::cCurrentModuleObject, DataIPShortCuts::cAlphaArgs(1), DataIPShortCuts::cAlphaArgs(5), DataIPShortCuts::cAlphaArgs(6), "Hot Water Nodes");

            {
                auto const SELECT_CASE_var(DataIPShortCuts::cAlphaArgs(7));
                if (SELECT_CASE_var == "CONSTANTFLOW") {
                    Boiler(BoilerNum).FlowMode = ConstantFlow;
                } else if (SELECT_CASE_var == "VARIABLEFLOW") { // backward compatible, clean out eventually
                    Boiler(BoilerNum).FlowMode = LeavingSetPointModulated;
                    ShowWarningError(RoutineName + DataIPShortCuts::cCurrentModuleObject + "=\"" + DataIPShortCuts::cAlphaArgs(1) + "\",");
                    ShowContinueError("Invalid " + DataIPShortCuts::cAlphaFieldNames(7) + '=' + DataIPShortCuts::cAlphaArgs(7));
                    ShowContinueError("Key choice is now called \"LeavingSetpointModulated\" and the simulation continues");
                } else if (SELECT_CASE_var == "LEAVINGSETPOINTMODULATED") {
                    Boiler(BoilerNum).FlowMode = LeavingSetPointModulated;
                } else if (SELECT_CASE_var == "NOTMODULATED") {
                    Boiler(BoilerNum).FlowMode = NotModulated;
                } else {
                    ShowSevereError(RoutineName + DataIPShortCuts::cCurrentModuleObject + "=\"" + DataIPShortCuts::cAlphaArgs(1) + "\",");
                    ShowContinueError("Invalid " + DataIPShortCuts::cAlphaFieldNames(7) + '=' + DataIPShortCuts::cAlphaArgs(7));
                    ShowContinueError("Available choices are ConstantFlow, NotModulated, or LeavingSetpointModulated");
                    ShowContinueError("Flow mode NotModulated is assumed and the simulation continues.");
                    // We will assume variable flow if not specified
                    Boiler(BoilerNum).FlowMode = NotModulated;
                }
            }

            if (NumAlphas > 7) {
                Boiler(BoilerNum).EndUseSubcategory = DataIPShortCuts::cAlphaArgs(8);
            } else {
                Boiler(BoilerNum).EndUseSubcategory = "Boiler"; // leave this as "boiler" instead of "general" like other end use subcategories since
                                                                // it appears this way in existing output files.
            }
        }

        if (ErrorsFound) {
            ShowFatalError(RoutineName + "Errors found in processing " + DataIPShortCuts::cCurrentModuleObject + " input.");
        }

        for (int BoilerNum = 1; BoilerNum <= NumBoilers; ++BoilerNum) {
            SetupOutputVariable(
                "Boiler Heating Rate", OutputProcessor::Unit::W, Boiler(BoilerNum).BoilerLoad, "System", "Average", Boiler(BoilerNum).Name);
            SetupOutputVariable("Boiler Heating Energy",
                                OutputProcessor::Unit::J,
                                Boiler(BoilerNum).BoilerEnergy,
                                "System",
                                "Sum",
                                Boiler(BoilerNum).Name,
                                _,
                                "ENERGYTRANSFER",
                                "BOILERS",
                                _,
                                "Plant");
            if (UtilityRoutines::SameString(Boiler(BoilerNum).BoilerFuelTypeForOutputVariable, "Electric")) {
                SetupOutputVariable("Boiler " + Boiler(BoilerNum).BoilerFuelTypeForOutputVariable + " Power",
                                    OutputProcessor::Unit::W,
                                    Boiler(BoilerNum).FuelUsed,
                                    "System",
                                    "Average",
                                    Boiler(BoilerNum).Name);
            } else {
                SetupOutputVariable("Boiler " + Boiler(BoilerNum).BoilerFuelTypeForOutputVariable + " Rate",
                                    OutputProcessor::Unit::W,
                                    Boiler(BoilerNum).FuelUsed,
                                    "System",
                                    "Average",
                                    Boiler(BoilerNum).Name);
            }
            SetupOutputVariable("Boiler " + Boiler(BoilerNum).BoilerFuelTypeForOutputVariable + " Energy",
                                OutputProcessor::Unit::J,
                                Boiler(BoilerNum).FuelConsumed,
                                "System",
                                "Sum",
                                Boiler(BoilerNum).Name,
                                _,
                                Boiler(BoilerNum).BoilerFuelTypeForOutputVariable,
                                "Heating",
                                Boiler(BoilerNum).EndUseSubcategory,
                                "Plant");
            SetupOutputVariable("Boiler Inlet Temperature",
                                OutputProcessor::Unit::C,
                                Boiler(BoilerNum).BoilerInletTemp,
                                "System",
                                "Average",
                                Boiler(BoilerNum).Name);
            SetupOutputVariable("Boiler Outlet Temperature",
                                OutputProcessor::Unit::C,
                                Boiler(BoilerNum).BoilerOutletTemp,
                                "System",
                                "Average",
                                Boiler(BoilerNum).Name);
            SetupOutputVariable(
                "Boiler Mass Flow Rate", OutputProcessor::Unit::kg_s, Boiler(BoilerNum).BoilerMassFlowRate, "System", "Average", Boiler(BoilerNum).Name);
            SetupOutputVariable("Boiler Ancillary Electric Power",
                                OutputProcessor::Unit::W,
                                Boiler(BoilerNum).ParasiticElecPower,
                                "System",
                                "Average",
                                Boiler(BoilerNum).Name);
            SetupOutputVariable("Boiler Ancillary Electric Energy",
                                OutputProcessor::Unit::J,
                                Boiler(BoilerNum).ParasiticElecConsumption,
                                "System",
                                "Sum",
                                Boiler(BoilerNum).Name,
                                _,
                                "ELECTRICITY",
                                "Heating",
                                "Boiler Parasitic",
                                "Plant");
            SetupOutputVariable("Boiler Part Load Ratio",
                                OutputProcessor::Unit::None,
                                Boiler(BoilerNum).BoilerPLR,
                                "System",
                                "Average",
                                Boiler(BoilerNum).Name);
            if (DataGlobals::AnyEnergyManagementSystemInModel) {
                SetupEMSInternalVariable("Boiler Nominal Capacity", Boiler(BoilerNum).Name, "[W]", Boiler(BoilerNum).NomCap);
            }
        }

    }

    void InitBoiler(int const BoilerNum) // number of the current boiler being simulated
    {

        // SUBROUTINE INFORMATION:
        //       AUTHOR         Fred Buhl
        //       DATE WRITTEN   April 2002
        //       MODIFIED       na
        //       RE-ENGINEERED  Brent Griffith, rework for plant upgrade

        // PURPOSE OF THIS SUBROUTINE:
        // This subroutine is for initializations of the Boiler components

        // METHODOLOGY EMPLOYED:
        // Uses the status flags to trigger initializations.

        // SUBROUTINE PARAMETER DEFINITIONS:
        static std::string const RoutineName("InitBoiler");

        // Init more variables
        if (Boiler(BoilerNum).MyFlag) {
            // Locate the boilers on the plant loops for later usage
            bool errFlag = false;
            PlantUtilities::ScanPlantLoopsForObject(Boiler(BoilerNum).Name,
                                                    DataPlant::TypeOf_Boiler_Simple,
                                    Boiler(BoilerNum).LoopNum,
                                    Boiler(BoilerNum).LoopSideNum,
                                    Boiler(BoilerNum).BranchNum,
                                    Boiler(BoilerNum).CompNum,
                                    errFlag,
                                    _,
                                    Boiler(BoilerNum).TempUpLimitBoilerOut,
                                    _,
                                    _,
                                    _);
            if (errFlag) {
                ShowFatalError("InitBoiler: Program terminated due to previous condition(s).");
            }

            if ((Boiler(BoilerNum).FlowMode == LeavingSetPointModulated) || (Boiler(BoilerNum).FlowMode == ConstantFlow)) {
                // reset flow priority
                DataPlant::PlantLoop(Boiler(BoilerNum).LoopNum)
                    .LoopSide(Boiler(BoilerNum).LoopSideNum)
                    .Branch(Boiler(BoilerNum).BranchNum)
                    .Comp(Boiler(BoilerNum).CompNum)
                    .FlowPriority = DataPlant::LoopFlowStatus_NeedyIfLoopOn;
            }

            Boiler(BoilerNum).MyFlag = false;
        }

        if (Boiler(BoilerNum).MyEnvrnFlag && DataGlobals::BeginEnvrnFlag && (DataPlant::PlantFirstSizesOkayToFinalize)) {
            // if ( ! PlantFirstSizeCompleted ) SizeBoiler( BoilerNum );
            Real64 const rho = FluidProperties::GetDensityGlycol(DataPlant::PlantLoop(Boiler(BoilerNum).LoopNum).FluidName,
                                   DataGlobals::HWInitConvTemp,
                                   DataPlant::PlantLoop(Boiler(BoilerNum).LoopNum).FluidIndex,
                                   RoutineName);
            Boiler(BoilerNum).DesMassFlowRate = Boiler(BoilerNum).VolFlowRate * rho;

            PlantUtilities::InitComponentNodes(0.0,
                               Boiler(BoilerNum).DesMassFlowRate,
                               Boiler(BoilerNum).BoilerInletNodeNum,
                               Boiler(BoilerNum).BoilerOutletNodeNum,
                               Boiler(BoilerNum).LoopNum,
                               Boiler(BoilerNum).LoopSideNum,
                               Boiler(BoilerNum).BranchNum,
                               Boiler(BoilerNum).CompNum);

            if (Boiler(BoilerNum).FlowMode == LeavingSetPointModulated) { // check if setpoint on outlet node
                if ((DataLoopNode::Node(Boiler(BoilerNum).BoilerOutletNodeNum).TempSetPoint == DataLoopNode::SensedNodeFlagValue) &&
                    (DataLoopNode::Node(Boiler(BoilerNum).BoilerOutletNodeNum).TempSetPointLo == DataLoopNode::SensedNodeFlagValue)) {
                    if (!DataGlobals::AnyEnergyManagementSystemInModel) {
                        if (!Boiler(BoilerNum).ModulatedFlowErrDone) {
                            ShowWarningError("Missing temperature setpoint for LeavingSetpointModulated mode Boiler named " + Boiler(BoilerNum).Name);
                            ShowContinueError(
                                "  A temperature setpoint is needed at the outlet node of a boiler in variable flow mode, use a SetpointManager");
                            ShowContinueError("  The overall loop setpoint will be assumed for Boiler. The simulation continues ... ");
                            Boiler(BoilerNum).ModulatedFlowErrDone = true;
                        }
                    } else {
                        // need call to EMS to check node
                        bool FatalError = false; // but not really fatal yet, but should be.
                        EMSManager::CheckIfNodeSetPointManagedByEMS(Boiler(BoilerNum).BoilerOutletNodeNum, EMSManager::iTemperatureSetPoint, FatalError);
                        if (FatalError) {
                            if (!Boiler(BoilerNum).ModulatedFlowErrDone) {
                                ShowWarningError("Missing temperature setpoint for LeavingSetpointModulated mode Boiler named " +
                                                 Boiler(BoilerNum).Name);
                                ShowContinueError("  A temperature setpoint is needed at the outlet node of a boiler in variable flow mode");
                                ShowContinueError("  use a Setpoint Manager to establish a setpoint at the boiler outlet node ");
                                ShowContinueError("  or use an EMS actuator to establish a setpoint at the boiler outlet node ");
                                ShowContinueError("  The overall loop setpoint will be assumed for Boiler. The simulation continues ... ");
                                Boiler(BoilerNum).ModulatedFlowErrDone = true;
                            }
                        }
                    }
                    Boiler(BoilerNum).ModulatedFlowSetToLoop = true; // this is for backward compatibility and could be removed
                }
            }

            Boiler(BoilerNum).MyEnvrnFlag = false;
        }

        if (!DataGlobals::BeginEnvrnFlag) {
            Boiler(BoilerNum).MyEnvrnFlag = true;
        }

        // every iteration inits.  (most in calc routine)

        if ((Boiler(BoilerNum).FlowMode == LeavingSetPointModulated) && Boiler(BoilerNum).ModulatedFlowSetToLoop) {
            // fix for clumsy old input that worked because loop setpoint was spread.
            //  could be removed with transition, testing , model change, period of being obsolete.
            {
                auto const SELECT_CASE_var(DataPlant::PlantLoop(Boiler(BoilerNum).LoopNum).LoopDemandCalcScheme);
                if (SELECT_CASE_var == DataPlant::SingleSetPoint) {
                    DataLoopNode::Node(Boiler(BoilerNum).BoilerOutletNodeNum).TempSetPoint =
                        DataLoopNode::Node(DataPlant::PlantLoop(Boiler(BoilerNum).LoopNum).TempSetPointNodeNum).TempSetPoint;
                } else if (SELECT_CASE_var == DataPlant::DualSetPointDeadBand) {
                    DataLoopNode::Node(Boiler(BoilerNum).BoilerOutletNodeNum).TempSetPointLo =
                        DataLoopNode::Node(DataPlant::PlantLoop(Boiler(BoilerNum).LoopNum).TempSetPointNodeNum).TempSetPointLo;
                }
            }
        }
    }

    void SizeBoiler(int const BoilerNum)
    {

        // SUBROUTINE INFORMATION:
        //       AUTHOR         Fred Buhl
        //       DATE WRITTEN   April 2002
        //       MODIFIED       November 2013 Daeho Kang, add component sizing table entries
        //       RE-ENGINEERED  na

        // PURPOSE OF THIS SUBROUTINE:
        // This subroutine is for sizing Boiler Components for which capacities and flow rates
        // have not been specified in the input.

        // METHODOLOGY EMPLOYED:
        // Obtains hot water flow rate from the plant sizing array. Calculates nominal capacity from
        // the hot water flow rate and the hot water loop design delta T.

        // SUBROUTINE PARAMETER DEFINITIONS:
        static std::string const RoutineName("SizeBoiler");

        // SUBROUTINE LOCAL VARIABLE DECLARATIONS:
        bool ErrorsFound(false); // If errors detected in input

        // grab some initial values for capacity and flow rate
        Real64 tmpNomCap = Boiler(BoilerNum).NomCap;  // local nominal capacity cooling power
        Real64 tmpBoilerVolFlowRate = Boiler(BoilerNum).VolFlowRate;  // local boiler design volume flow rate

        int const PltSizNum = DataPlant::PlantLoop(Boiler(BoilerNum).LoopNum).PlantSizNum;  // Plant Sizing index corresponding to CurLoopNum

        if (PltSizNum > 0) {
            if (DataSizing::PlantSizData(PltSizNum).DesVolFlowRate >= DataHVACGlobals::SmallWaterVolFlow) {

                Real64 const rho = FluidProperties::GetDensityGlycol(DataPlant::PlantLoop(Boiler(BoilerNum).LoopNum).FluidName,
                                       DataGlobals::HWInitConvTemp,
                                       DataPlant::PlantLoop(Boiler(BoilerNum).LoopNum).FluidIndex,
                                       RoutineName);
                Real64 const Cp = FluidProperties::GetSpecificHeatGlycol(DataPlant::PlantLoop(Boiler(BoilerNum).LoopNum).FluidName,
                                           DataGlobals::HWInitConvTemp,
                                           DataPlant::PlantLoop(Boiler(BoilerNum).LoopNum).FluidIndex,
                                           RoutineName);
                tmpNomCap = Cp * rho * Boiler(BoilerNum).SizFac * DataSizing::PlantSizData(PltSizNum).DeltaT * DataSizing::PlantSizData(PltSizNum).DesVolFlowRate;
            } else {
                if (Boiler(BoilerNum).NomCapWasAutoSized) tmpNomCap = 0.0;
            }
            if (DataPlant::PlantFirstSizesOkayToFinalize) {
                if (Boiler(BoilerNum).NomCapWasAutoSized) {
                    Boiler(BoilerNum).NomCap = tmpNomCap;
                    if (DataPlant::PlantFinalSizesOkayToReport) {
                        ReportSizingManager::ReportSizingOutput("Boiler:HotWater", Boiler(BoilerNum).Name, "Design Size Nominal Capacity [W]", tmpNomCap);
                    }
                    if (DataPlant::PlantFirstSizesOkayToReport) {
                        ReportSizingManager::ReportSizingOutput("Boiler:HotWater", Boiler(BoilerNum).Name, "Initial Design Size Nominal Capacity [W]", tmpNomCap);
                    }
                } else { // Hard-sized with sizing data
                    if (Boiler(BoilerNum).NomCap > 0.0 && tmpNomCap > 0.0) {
                        Real64 const NomCapUser = Boiler(BoilerNum).NomCap;  // Hardsized nominal capacity for reporting
                        if (DataPlant::PlantFinalSizesOkayToReport) {
                            ReportSizingManager::ReportSizingOutput("Boiler:HotWater",
                                               Boiler(BoilerNum).Name,
                                               "Design Size Nominal Capacity [W]",
                                               tmpNomCap,
                                               "User-Specified Nominal Capacity [W]",
                                               NomCapUser);
                            if (DataGlobals::DisplayExtraWarnings) {
                                if ((std::abs(tmpNomCap - NomCapUser) / NomCapUser) > DataSizing::AutoVsHardSizingThreshold) {
                                    ShowMessage("SizeBoilerHotWater: Potential issue with equipment sizing for " + Boiler(BoilerNum).Name);
                                    ShowContinueError("User-Specified Nominal Capacity of " + General::RoundSigDigits(NomCapUser, 2) + " [W]");
                                    ShowContinueError("differs from Design Size Nominal Capacity of " + General::RoundSigDigits(tmpNomCap, 2) + " [W]");
                                    ShowContinueError("This may, or may not, indicate mismatched component sizes.");
                                    ShowContinueError("Verify that the value entered is intended and is consistent with other components.");
                                }
                            }
                        }
                    }
                }
            }
        } else {
            if (Boiler(BoilerNum).NomCapWasAutoSized && DataPlant::PlantFirstSizesOkayToFinalize) {
                ShowSevereError("Autosizing of Boiler nominal capacity requires a loop Sizing:Plant object");
                ShowContinueError("Occurs in Boiler object=" + Boiler(BoilerNum).Name);
                ErrorsFound = true;
            }
            if (!Boiler(BoilerNum).NomCapWasAutoSized && DataPlant::PlantFinalSizesOkayToReport &&
                (Boiler(BoilerNum).NomCap > 0.0)) { // Hard-sized with no sizing data
                ReportSizingManager::ReportSizingOutput("Boiler:HotWater", Boiler(BoilerNum).Name, "User-Specified Nominal Capacity [W]", Boiler(BoilerNum).NomCap);
            }
        }

        if (PltSizNum > 0) {
            if (DataSizing::PlantSizData(PltSizNum).DesVolFlowRate >= DataHVACGlobals::SmallWaterVolFlow) {
                tmpBoilerVolFlowRate = DataSizing::PlantSizData(PltSizNum).DesVolFlowRate * Boiler(BoilerNum).SizFac;
            } else {
                if (Boiler(BoilerNum).VolFlowRateWasAutoSized) tmpBoilerVolFlowRate = 0.0;
            }
            if (DataPlant::PlantFirstSizesOkayToFinalize) {
                if (Boiler(BoilerNum).VolFlowRateWasAutoSized) {
                    Boiler(BoilerNum).VolFlowRate = tmpBoilerVolFlowRate;
                    if (DataPlant::PlantFinalSizesOkayToReport) {
                        ReportSizingManager::ReportSizingOutput(
                            "Boiler:HotWater", Boiler(BoilerNum).Name, "Design Size Design Water Flow Rate [m3/s]", tmpBoilerVolFlowRate);
                    }
                    if (DataPlant::PlantFirstSizesOkayToReport) {
                        ReportSizingManager::ReportSizingOutput(
                            "Boiler:HotWater", Boiler(BoilerNum).Name, "Initial Design Size Design Water Flow Rate [m3/s]", tmpBoilerVolFlowRate);
                    }
                } else {
                    if (Boiler(BoilerNum).VolFlowRate > 0.0 && tmpBoilerVolFlowRate > 0.0) {
                        Real64 VolFlowRateUser = Boiler(BoilerNum).VolFlowRate;  // Hardsized volume flow for reporting
                        if (DataPlant::PlantFinalSizesOkayToReport) {
                            ReportSizingManager::ReportSizingOutput("Boiler:HotWater",
                                               Boiler(BoilerNum).Name,
                                               "Design Size Design Water Flow Rate [m3/s]",
                                               tmpBoilerVolFlowRate,
                                               "User-Specified Design Water Flow Rate [m3/s]",
                                               VolFlowRateUser);
                            if (DataGlobals::DisplayExtraWarnings) {
                                if ((std::abs(tmpBoilerVolFlowRate - VolFlowRateUser) / VolFlowRateUser) > DataSizing::AutoVsHardSizingThreshold) {
                                    ShowMessage("SizeBoilerHotWater: Potential issue with equipment sizing for " + Boiler(BoilerNum).Name);
                                    ShowContinueError("User-Specified Design Water Flow Rate of " + General::RoundSigDigits(VolFlowRateUser, 2) + " [m3/s]");
                                    ShowContinueError("differs from Design Size Design Water Flow Rate of " +
                                                          General::RoundSigDigits(tmpBoilerVolFlowRate, 2) + " [m3/s]");
                                    ShowContinueError("This may, or may not, indicate mismatched component sizes.");
                                    ShowContinueError("Verify that the value entered is intended and is consistent with other components.");
                                }
                            }
                        }
                        tmpBoilerVolFlowRate = VolFlowRateUser;
                    }
                }
            }
        } else {
            if (Boiler(BoilerNum).VolFlowRateWasAutoSized && DataPlant::PlantFirstSizesOkayToFinalize) {
                ShowSevereError("Autosizing of Boiler design flow rate requires a loop Sizing:Plant object");
                ShowContinueError("Occurs in Boiler object=" + Boiler(BoilerNum).Name);
                ErrorsFound = true;
            }
            if (!Boiler(BoilerNum).VolFlowRateWasAutoSized && DataPlant::PlantFinalSizesOkayToReport &&
                (Boiler(BoilerNum).VolFlowRate > 0.0)) { // Hard-sized with no sizing data
                ReportSizingManager::ReportSizingOutput(
                    "Boiler:HotWater", Boiler(BoilerNum).Name, "User-Specified Design Water Flow Rate [m3/s]", Boiler(BoilerNum).VolFlowRate);
            }
        }

        PlantUtilities::RegisterPlantCompDesignFlow(Boiler(BoilerNum).BoilerInletNodeNum, tmpBoilerVolFlowRate);

        if (DataPlant::PlantFinalSizesOkayToReport) {
            // create predefined report
            std::string const equipName = Boiler(BoilerNum).Name;
            OutputReportPredefined::PreDefTableEntry(OutputReportPredefined::pdchMechType, equipName, "Boiler:HotWater");
            OutputReportPredefined::PreDefTableEntry(OutputReportPredefined::pdchMechNomEff, equipName, Boiler(BoilerNum).Effic);
            OutputReportPredefined::PreDefTableEntry(OutputReportPredefined::pdchMechNomCap, equipName, Boiler(BoilerNum).NomCap);
        }

        if (ErrorsFound) {
            ShowFatalError("Preceding sizing errors cause program termination");
        }
    }

    void CalcBoilerModel(int &BoilerNum,         // boiler identifier
                         Real64 const MyLoad,    // W - hot water demand to be met by boiler
                         bool const RunFlag,     // TRUE if boiler operating
                         int const EquipFlowCtrl // Flow control mode for the equipment
    )
    {
        // SUBROUTINE INFORMATION:
        //       AUTHOR         Dan Fisher
        //       DATE WRITTEN   April 1999
        //       MODIFIED       Taecheol Kim,May 2000
        //                      Jun. 2008, R. Raustad, FSEC. Added boiler efficiency curve object
        //                      Aug. 2011, B. Griffith, NREL. Added switch for temperature to use in curve
        //                      Nov. 2016, R. Zhang, LBNL. Applied the boiler fouling fault model
        //       RE-ENGINEERED  na

        // PURPOSE OF THIS SUBROUTINE:
        // This subroutine calculates the boiler fuel consumption and the associated
        // hot water demand met by the boiler

        // METHODOLOGY EMPLOYED:
        // The model is based on a single combustion efficiency (=1 for electric)
        // and a second order polynomial fit of performance data to obtain part
        // load performance

        // SUBROUTINE PARAMETER DEFINITIONS:
        static std::string const RoutineName("CalcBoilerModel");

        // clean up some operating conditions, may not be necessary
        Boiler(BoilerNum).BoilerLoad = 0.0;
        Boiler(BoilerNum).ParasiticElecPower = 0.0;
        Boiler(BoilerNum).BoilerMassFlowRate = 0.0;

        int const BoilerInletNode = Boiler(BoilerNum).BoilerInletNodeNum;
        int const BoilerOutletNode = Boiler(BoilerNum).BoilerOutletNodeNum;
        Real64 BoilerNomCap = Boiler(BoilerNum).NomCap;  // W - boiler nominal capacity
        Real64 const BoilerMaxPLR = Boiler(BoilerNum).MaxPartLoadRat;  // boiler maximum part load ratio
        Real64 const BoilerMinPLR = Boiler(BoilerNum).MinPartLoadRat;  // boiler minimum part load ratio
        Real64 BoilerEff = Boiler(BoilerNum).Effic;  // boiler efficiency
        Real64 const TempUpLimitBout = Boiler(BoilerNum).TempUpLimitBoilerOut;  // C - boiler high temperature limit
        Real64 const BoilerMassFlowRateMax = Boiler(BoilerNum).DesMassFlowRate;  // Max Design Boiler Mass Flow Rate converted from Volume Flow Rate
        Real64 const ParasiticElecLoad = Boiler(BoilerNum).ParasiticElecLoad;  // Boiler parasitic electric power at full load
        int const LoopNum = Boiler(BoilerNum).LoopNum;
        int const LoopSideNum = Boiler(BoilerNum).LoopSideNum;

        Real64 Cp = FluidProperties::GetSpecificHeatGlycol(
            DataPlant::PlantLoop(Boiler(BoilerNum).LoopNum).FluidName, DataLoopNode::Node(BoilerInletNode).Temp, DataPlant::PlantLoop(Boiler(BoilerNum).LoopNum).FluidIndex, RoutineName);

        // If the specified load is 0.0 or the boiler should not run then we leave this subroutine. Before leaving
        // if the component control is SERIESACTIVE we set the component flow to inlet flow so that flow resolver
        // will not shut down the branch
        if (MyLoad <= 0.0 || !RunFlag) {
            if (EquipFlowCtrl == DataBranchAirLoopPlant::ControlType_SeriesActive) Boiler(BoilerNum).BoilerMassFlowRate = DataLoopNode::Node(BoilerInletNode).MassFlowRate;
            return;
        }

        // If there is a fault of boiler fouling (zrp_Nov2016)
        if (Boiler(BoilerNum).FaultyBoilerFoulingFlag && (!DataGlobals::WarmupFlag) && (!DataGlobals::DoingSizing) && (!DataGlobals::KickOffSimulation)) {
            int FaultIndex = Boiler(BoilerNum).FaultyBoilerFoulingIndex;
            Real64 NomCap_ff = BoilerNomCap;
            Real64 BoilerEff_ff = BoilerEff;

            // calculate the Faulty Boiler Fouling Factor using fault information
            Boiler(BoilerNum).FaultyBoilerFoulingFactor = FaultsManager::FaultsBoilerFouling(FaultIndex).CalFoulingFactor();

            // update the boiler nominal capacity at faulty cases
            BoilerNomCap = NomCap_ff * Boiler(BoilerNum).FaultyBoilerFoulingFactor;
            BoilerEff = BoilerEff_ff * Boiler(BoilerNum).FaultyBoilerFoulingFactor;
        }

        // Set the current load equal to the boiler load
        Boiler(BoilerNum).BoilerLoad = MyLoad;

        // Initialize the delta temperature to zero
        Real64 BoilerDeltaTemp; // C - boiler inlet to outlet temperature difference, set in all necessary code paths so no initialization required

        if (DataPlant::PlantLoop(LoopNum).LoopSide(LoopSideNum).FlowLock == 0) {
            // Either set the flow to the Constant value or calculate the flow for the variable volume
            if ((Boiler(BoilerNum).FlowMode == ConstantFlow) || (Boiler(BoilerNum).FlowMode == NotModulated)) {
                // Then find the flow rate and outlet temp
                Boiler(BoilerNum).BoilerMassFlowRate = BoilerMassFlowRateMax;
                PlantUtilities::SetComponentFlowRate(Boiler(BoilerNum).BoilerMassFlowRate,
                                     BoilerInletNode,
                                     BoilerOutletNode,
                                     Boiler(BoilerNum).LoopNum,
                                     Boiler(BoilerNum).LoopSideNum,
                                     Boiler(BoilerNum).BranchNum,
                                     Boiler(BoilerNum).CompNum);

                if ((Boiler(BoilerNum).BoilerMassFlowRate != 0.0) && (MyLoad > 0.0)) {
                    BoilerDeltaTemp = Boiler(BoilerNum).BoilerLoad / Boiler(BoilerNum).BoilerMassFlowRate / Cp;
                } else {
                    BoilerDeltaTemp = 0.0;
                }

                Boiler(BoilerNum).BoilerOutletTemp = BoilerDeltaTemp + DataLoopNode::Node(BoilerInletNode).Temp;

            } else if (Boiler(BoilerNum).FlowMode == LeavingSetPointModulated) {
                // Calculate the Delta Temp from the inlet temp to the boiler outlet setpoint
                // Then find the flow rate and outlet temp

                {
                    auto const SELECT_CASE_var(DataPlant::PlantLoop(Boiler(BoilerNum).LoopNum).LoopDemandCalcScheme);
                    if (SELECT_CASE_var == DataPlant::SingleSetPoint) {
                        BoilerDeltaTemp = DataLoopNode::Node(BoilerOutletNode).TempSetPoint - DataLoopNode::Node(BoilerInletNode).Temp;
                    } else if (SELECT_CASE_var == DataPlant::DualSetPointDeadBand) {
                        BoilerDeltaTemp = DataLoopNode::Node(BoilerOutletNode).TempSetPointLo - DataLoopNode::Node(BoilerInletNode).Temp;
                    } else {
                        assert(false);
                    }
                }

                Boiler(BoilerNum).BoilerOutletTemp = BoilerDeltaTemp + DataLoopNode::Node(BoilerInletNode).Temp;

                if ((BoilerDeltaTemp > 0.0) && (Boiler(BoilerNum).BoilerLoad > 0.0)) {
                    Boiler(BoilerNum).BoilerMassFlowRate = Boiler(BoilerNum).BoilerLoad / Cp / BoilerDeltaTemp;

                    Boiler(BoilerNum).BoilerMassFlowRate = min(BoilerMassFlowRateMax, Boiler(BoilerNum).BoilerMassFlowRate);

                } else {
                    Boiler(BoilerNum).BoilerMassFlowRate = 0.0;
                }
                PlantUtilities::SetComponentFlowRate(Boiler(BoilerNum).BoilerMassFlowRate,
                                     BoilerInletNode,
                                     BoilerOutletNode,
                                     Boiler(BoilerNum).LoopNum,
                                     Boiler(BoilerNum).LoopSideNum,
                                     Boiler(BoilerNum).BranchNum,
                                     Boiler(BoilerNum).CompNum);

            } // End of Constant/Variable Flow If Block

        } else { // If FlowLock is True
            // Set the boiler flow rate from inlet node and then check performance
            Boiler(BoilerNum).BoilerMassFlowRate = DataLoopNode::Node(BoilerInletNode).MassFlowRate;

            if ((MyLoad > 0.0) && (Boiler(BoilerNum).BoilerMassFlowRate > 0.0)) { // this boiler has a heat load
                Boiler(BoilerNum).BoilerLoad = MyLoad;
                if (Boiler(BoilerNum).BoilerLoad > BoilerNomCap * BoilerMaxPLR) Boiler(BoilerNum).BoilerLoad = BoilerNomCap * BoilerMaxPLR;
                if (Boiler(BoilerNum).BoilerLoad < BoilerNomCap * BoilerMinPLR) Boiler(BoilerNum).BoilerLoad = BoilerNomCap * BoilerMinPLR;
                Boiler(BoilerNum).BoilerOutletTemp = DataLoopNode::Node(BoilerInletNode).Temp + Boiler(BoilerNum).BoilerLoad / (Boiler(BoilerNum).BoilerMassFlowRate * Cp);
            } else {
                Boiler(BoilerNum).BoilerLoad = 0.0;
                Boiler(BoilerNum).BoilerOutletTemp = DataLoopNode::Node(BoilerInletNode).Temp;
            }

        } // End of the FlowLock If block

        // Limit BoilerOutletTemp.  If > max temp, trip boiler off
        if (Boiler(BoilerNum).BoilerOutletTemp > TempUpLimitBout) {
            Boiler(BoilerNum).BoilerLoad = 0.0;
            Boiler(BoilerNum).BoilerOutletTemp = DataLoopNode::Node(BoilerInletNode).Temp;
        }
        Real64 OperPLR = Boiler(BoilerNum).BoilerLoad / BoilerNomCap;  // operating part load ratio
        OperPLR = min(OperPLR, BoilerMaxPLR);
        OperPLR = max(OperPLR, BoilerMinPLR);

        // set report variable
        Boiler(BoilerNum).BoilerPLR = OperPLR;

        // calculate theoretical fuel use based on nominal thermal efficiency
        Real64 const TheorFuelUse = Boiler(BoilerNum).BoilerLoad / BoilerEff;  // Theoretical (stoichiometric) fuel use
        Real64 EffCurveOutput = 1.0;  // Output of boiler efficiency curve

        // calculate normalized efficiency based on curve object type
        if (Boiler(BoilerNum).EfficiencyCurvePtr > 0) {
            if (CurveManager::PerfCurve(Boiler(BoilerNum).EfficiencyCurvePtr).NumDims == 2) {
                if (Boiler(BoilerNum).CurveTempMode == EnteringBoilerTemp) {
                    EffCurveOutput = CurveManager::CurveValue(Boiler(BoilerNum).EfficiencyCurvePtr, OperPLR, DataLoopNode::Node(BoilerInletNode).Temp);
                } else if (Boiler(BoilerNum).CurveTempMode == LeavingBoilerTemp) {
                    EffCurveOutput = CurveManager::CurveValue(Boiler(BoilerNum).EfficiencyCurvePtr, OperPLR, Boiler(BoilerNum).BoilerOutletTemp);
                }
            } else {
                EffCurveOutput = CurveManager::CurveValue(Boiler(BoilerNum).EfficiencyCurvePtr, OperPLR);
            }
        }

        // warn if efficiency curve produces zero or negative results
        if (!DataGlobals::WarmupFlag && EffCurveOutput <= 0.0) {
            if (Boiler(BoilerNum).BoilerLoad > 0.0) {
                if (Boiler(BoilerNum).EffCurveOutputError < 1) {
                    ++Boiler(BoilerNum).EffCurveOutputError;
                    ShowWarningError("Boiler:HotWater \"" + Boiler(BoilerNum).Name + "\"");
                    ShowContinueError("...Normalized Boiler Efficiency Curve output is less than or equal to 0.");
                    ShowContinueError("...Curve input x value (PLR)     = " + General::TrimSigDigits(OperPLR, 5));
                    if (CurveManager::PerfCurve(Boiler(BoilerNum).EfficiencyCurvePtr).NumDims == 2) {
                        if (Boiler(BoilerNum).CurveTempMode == EnteringBoilerTemp) {
                            ShowContinueError("...Curve input y value (Tinlet) = " + General::TrimSigDigits(DataLoopNode::Node(BoilerInletNode).Temp, 2));
                        } else if (Boiler(BoilerNum).CurveTempMode == LeavingBoilerTemp) {
                            ShowContinueError("...Curve input y value (Toutlet) = " + General::TrimSigDigits(Boiler(BoilerNum).BoilerOutletTemp, 2));
                        }
                    }
                    ShowContinueError("...Curve output (normalized eff) = " + General::TrimSigDigits(EffCurveOutput, 5));
                    ShowContinueError("...Calculated Boiler efficiency  = " + General::TrimSigDigits(EffCurveOutput * BoilerEff, 5) +
                                      " (Boiler efficiency = Nominal Thermal Efficiency * Normalized Boiler Efficiency Curve output)");
                    ShowContinueErrorTimeStamp("...Curve output reset to 0.01 and simulation continues.");
                } else {
                    ShowRecurringWarningErrorAtEnd("Boiler:HotWater \"" + Boiler(BoilerNum).Name +
                                                       "\": Boiler Efficiency Curve output is less than or equal to 0 warning continues...",
                                                   Boiler(BoilerNum).EffCurveOutputIndex,
                                                   EffCurveOutput,
                                                   EffCurveOutput);
                }
            }
            EffCurveOutput = 0.01;
        }

        // warn if overall efficiency greater than 1.1
        if (!DataGlobals::WarmupFlag && EffCurveOutput * BoilerEff > 1.1) {
            if (Boiler(BoilerNum).BoilerLoad > 0.0 && Boiler(BoilerNum).EfficiencyCurvePtr > 0) {
                if (Boiler(BoilerNum).CalculatedEffError < 1) {
                    ++Boiler(BoilerNum).CalculatedEffError;
                    ShowWarningError("Boiler:HotWater \"" + Boiler(BoilerNum).Name + "\"");
                    ShowContinueError("...Calculated Boiler Efficiency is greater than 1.1.");
                    ShowContinueError("...Boiler Efficiency calculations shown below.");
                    ShowContinueError("...Curve input x value (PLR)     = " + General::TrimSigDigits(OperPLR, 5));
                    if (CurveManager::PerfCurve(Boiler(BoilerNum).EfficiencyCurvePtr).NumDims == 2) {
                        if (Boiler(BoilerNum).CurveTempMode == EnteringBoilerTemp) {
                            ShowContinueError("...Curve input y value (Tinlet) = " + General::TrimSigDigits(DataLoopNode::Node(BoilerInletNode).Temp, 2));
                        } else if (Boiler(BoilerNum).CurveTempMode == LeavingBoilerTemp) {
                            ShowContinueError("...Curve input y value (Toutlet) = " + General::TrimSigDigits(Boiler(BoilerNum).BoilerOutletTemp, 2));
                        }
                    }
                    ShowContinueError("...Curve output (normalized eff) = " + General::TrimSigDigits(EffCurveOutput, 5));
                    ShowContinueError("...Calculated Boiler efficiency  = " + General::TrimSigDigits(EffCurveOutput * BoilerEff, 5) +
                                      " (Boiler efficiency = Nominal Thermal Efficiency * Normalized Boiler Efficiency Curve output)");
                    ShowContinueErrorTimeStamp("...Curve output reset to 1.1 and simulation continues.");
                } else {
                    ShowRecurringWarningErrorAtEnd("Boiler:HotWater \"" + Boiler(BoilerNum).Name +
                                                       "\": Calculated Boiler Efficiency is greater than 1.1 warning continues...",
                                                   Boiler(BoilerNum).CalculatedEffIndex,
                                                   EffCurveOutput * BoilerEff,
                                                   EffCurveOutput * BoilerEff);
                }
            }
            EffCurveOutput = 1.1;
        }

        // calculate fuel used based on normalized boiler efficiency curve (=1 when no curve used)
        Boiler(BoilerNum).FuelUsed = TheorFuelUse / EffCurveOutput;
        if (Boiler(BoilerNum).BoilerLoad > 0.0) Boiler(BoilerNum).ParasiticElecPower = ParasiticElecLoad * OperPLR;
    }

    void UpdateBoilerRecords(Real64 const MyLoad, // boiler operating load
                             bool const RunFlag,  // boiler on when TRUE
                             int const Num        // boiler number
    )
    {
        // SUBROUTINE INFORMATION:
        //       AUTHOR:          Dan Fisher
        //       DATE WRITTEN:    October 1998

        // PURPOSE OF THIS SUBROUTINE:
        // boiler simulation reporting

        Real64 const ReportingConstant = DataHVACGlobals::TimeStepSys * DataGlobals::SecInHour;
        int const BoilerInletNode = Boiler(Num).BoilerInletNodeNum;
        int const BoilerOutletNode = Boiler(Num).BoilerOutletNodeNum;

        if (MyLoad <= 0 || !RunFlag) {
            PlantUtilities::SafeCopyPlantNode(BoilerInletNode, BoilerOutletNode);
            DataLoopNode::Node(BoilerOutletNode).Temp = DataLoopNode::Node(BoilerInletNode).Temp;
            Boiler(Num).BoilerOutletTemp = DataLoopNode::Node(BoilerInletNode).Temp;
            Boiler(Num).BoilerLoad = 0.0;
            Boiler(Num).FuelUsed = 0.0;
            Boiler(Num).ParasiticElecPower = 0.0;
            Boiler(Num).BoilerPLR = 0.0;
        } else {
            PlantUtilities::SafeCopyPlantNode(BoilerInletNode, BoilerOutletNode);
            DataLoopNode::Node(BoilerOutletNode).Temp = Boiler(Num).BoilerOutletTemp;
        }

        Boiler(Num).BoilerInletTemp = DataLoopNode::Node(BoilerInletNode).Temp;
        Boiler(Num).BoilerMassFlowRate = DataLoopNode::Node(BoilerOutletNode).MassFlowRate;
        Boiler(Num).BoilerEnergy = Boiler(Num).BoilerLoad * ReportingConstant;
        Boiler(Num).FuelConsumed = Boiler(Num).FuelUsed * ReportingConstant;
        Boiler(Num).ParasiticElecConsumption = Boiler(Num).ParasiticElecPower * ReportingConstant;
    }

} // namespace Boilers

} // namespace EnergyPlus
