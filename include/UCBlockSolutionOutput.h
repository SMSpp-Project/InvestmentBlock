/*--------------------------------------------------------------------------*/
/*---------------------- File UCBlockSolutionOutput.h ----------------------*/
/*--------------------------------------------------------------------------*/
/** @file
 *
 * The UCBlockSolutionOutput is a convenient class to export the solution of a
 * UCBlock in the form of CSV files. The exported solution contains the values
 * of the variables of the UnitBlock and the NetworkBlock, as well as, the
 * dual values of some constraints of the UCBlock and the NetworkBlock. To
 * identify the data associated with a Block, we use the name of the Block. If
 * the string returned by the name() method of the Block is not empty, it is
 * used as the name of the Block. Otherwise, the class name of the Block
 * (returned by the classname() method) is used. The solution is output in
 * multiple files, each one containing one type of data. The files containing
 * the active power, primary spinning reserve, and secondary spinning reserve
 * have the following format:
 *
 *   Timestep , Name_0     , Name_1     , ... , Name_k
 *   0        , v_0_0      , v_0_1      , ... , v_0_k
 *   1        , v_1_0      , v_1_1      , ... , v_1_k
 *   ...
 *   T-1      , v_{T-1}_0  , v_{T-1}_1  , ... , v_{T-1}_k
 *
 * where
 *
 * - T is the time horizon;
 *
 * - Name_j is the name of a generator. If the Block has a single generator,
 *   the name of the generator is the name of the Block. If the Block has
 *   multiple generators, the string "_i" is appended to the name of the Block
 *   to indicate the data associated with its i-th generator;
 *
 * - val_t_j is the value (active power, primary spinning reserve, or
 *   secondary spinning reserve) at time t for the generator whose name is
 *   Name_j.
 *
 * These files contain the data of each UnitBlock (that is not a
 * HydroSystemUnitBlock) of the UCBlock (including every HydroUnitBlock of all
 * HydroSystemUnitBlock). The default names for these files are
 * ActivePowerOUT.csv, PrimaryOUT.csv, and SecondaryOUT.csv.
 *
 * The volumes of the reservoirs (of all HydroUnitBlock and BatteryUnitBlock)
 * are output to a file with a similar format, whose default name is
 * VolumeOUT.csv. The only difference is that the data (i.e., the volumes) is
 * associated with a reservoir. If the Block has a single reservoir, then
 * Name_j is the name of the Block. If the Block has multiple reservoirs, then
 * "_i" is appended to the name of the Block to form Name_j in order to
 * identify the data associated with the i-th reservoir of the Block.
 *
 * The power flow of the NetworkBlock are output to a file named FlowsOUT.csv
 * having the following format:
 *
 *   Timestep , Line_0     , Line_1     , ... , Line_m
 *   0        , v_0_0      , v_0_1      , ... , v_0_m
 *   1        , v_1_0      , v_1_1      , ... , v_1_m
 *   ...
 *   T-1      , v_{T-1}_0  , v_{T-1}_1  , ... , v_{T-1}_m
 *
 * where
 *
 * - T is the time horizon;
 *
 * - m+1 is the number of lines in the network (which is assumed to be
 *   constant over time);
 *
 * - v_t_j is the power flow at time t at line j.
 *
 * The dual values of the following constraints are also part of the
 * output. The dual values of each constraint is output to a dedicated file,
 * whose default name is indicated between parentheses.
 *
 * - power flow limit (MarginalCostFlowsOUT.csv);
 * - node injection (MarginalCostActivePowerDemandOUT.csv);
 * - primary demand (MarginalCostPrimaryOUT.csv);
 * - secondary demand (MarginalCostSecondaryOUT.csv);
 * - inertia demand (MarginalCostInertiaOUT.csv);
 * - maximum pollutant emission (MarginalPollutant_pOUT.csv, for each
 *   pollutant p in {0, ..., number_of_pollutants - 1}).
 *
 * The files containing the dual values of the power flow limit and the node
 * injection constraints have the following format:
 *
 *   Timestep , Node_0     , Node_1     , ... , Node_n
 *   0        , v_0_0      , v_0_1      , ... , v_0_n
 *   1        , v_1_0      , v_1_1      , ... , v_1_n
 *   ...
 *   T-1      , v_{T-1}_0  , v_{T-1}_1  , ... , v_{T-1}_n
 *
 * where
 *
 * - T is the time horizon;
 *
 * - n+1 is the number of nodes;
 *
 * - val_t_j is the dual value of the constraint (power flow limit or node
 *   injection constraint) associated with time t and node j.
 *
 * The format of the files for the primary demand, secondary demand, and
 * inertia demand is very similar:
 *
 *   Timestep , Zone_0     , Zone_1     , ... , Zone_z
 *   0        , v_0_0      , v_0_1      , ... , v_0_z
 *   1        , v_1_0      , v_1_1      , ... , v_1_z
 *   ...
 *   T-1      , v_{T-1}_0  , v_{T-1}_1  , ... , v_{T-1}_z
 *
 * where
 *
 * - T is the time horizon;
 *
 * - z+1 is the number of zones;
 *
 * - val_t_j is the dual value of the constraint (primary demand, secondary
 *   demand, or inertia demand) associated with time t and zone j.
 *
 * Finally, the dual values of the maximum pollutant emission constraints are
 * output in multiple files, each one for a pollutant. For each p in {0, ...,
 * number_of_pollutants - 1}, the file containing the dual values of the
 * maximum pollutant emission constraints for the pollutant p has the
 * following format:
 *
 *   Zone_0 , Zone_1 , ... , Zone_pz
 *   v_0    , v_1    , ... , v_pz
 *
 * where
 *
 * - pz+1 is the number of zones for pollutant p and v_j is the dual value of
 *   the constraint associated with zone j.
 *
 * \author Rafael Durbano Lobato \n
 *         Dipartimento di Informatica \n
 *         Universita' di Pisa \n
 *
 * \copyright &copy; by Rafael Durbano Lobato
 */

/*--------------------------------------------------------------------------*/
/*----------------------------- DEFINITIONS --------------------------------*/
/*--------------------------------------------------------------------------*/

#ifndef __UCBlockSolutionOutput
#define __UCBlockSolutionOutput

/*--------------------------------------------------------------------------*/
/*------------------------------ INCLUDES ----------------------------------*/
/*--------------------------------------------------------------------------*/

#include "BatteryUnitBlock.h"
#include "DCNetworkBlock.h"
#include "HydroSystemUnitBlock.h"
#include "IntermittentUnitBlock.h"
#include "SlackUnitBlock.h"
#include "ThermalUnitBlock.h"
#include "UCBlock.h"

#include <filesystem>
#include <iomanip>
#include <iostream>

/*--------------------------------------------------------------------------*/
/*--------------------------- NAMESPACE ------------------------------------*/
/*--------------------------------------------------------------------------*/

using namespace SMSpp_di_unipi_it;

/*--------------------------------------------------------------------------*/
/*---------------------- CLASS UCBlockSolutionOutput -----------------------*/
/*--------------------------------------------------------------------------*/

class UCBlockSolutionOutput
{

/*--------------------------------------------------------------------------*/
/*----------------------- PUBLIC PART OF THE CLASS -------------------------*/
/*--------------------------------------------------------------------------*/

 public:

/*--------------------------------------------------------------------------*/
/*---------------------------- PUBLIC TYPES --------------------------------*/
/*--------------------------------------------------------------------------*/

 using Index = Block::Index;

/*--------------------------------------------------------------------------*/
/*--------------------- PUBLIC METHODS OF THE CLASS ------------------------*/
/*--------------------------------------------------------------------------*/

 UCBlockSolutionOutput() {
  filenames.resize( number_of_files );

  auto extension = "OUT.csv";

  // Variables

  filenames[ active_power ] = { "ActivePower" , extension };
  filenames[ primary_spinning_reserve ] = { "Primary" , extension };
  filenames[ secondary_spinning_reserve ] = { "Secondary" , extension };
  filenames[ volume ] = { "Volume" , extension };
  filenames[ flow ] = { "Flows" , extension };
  filenames[ marginal_cost_active_power_demand ] =
   { "MarginalCostActivePowerDemand" , extension };
  filenames[ marginal_cost_primary ] = { "MarginalCostPrimary" , extension };
  filenames[ marginal_cost_secondary ] = { "MarginalCostSecondary" ,
                                           extension };
  filenames[ marginal_cost_inertia ] = { "MarginalCostInertia" , extension };
  filenames[ marginal_cost_flows ] = { "MarginalCostFlows" , extension };
  filenames[ marginal_pollutant ] = { "MarginalPollutant_" , extension };

  // Data

  filenames[ demand ] = { "Demand" , extension };
  filenames[ max_power ] = { "MaxPower" , extension };
  filenames[ inflow ] = { "Inflows" , extension };
  filenames[ flow_rate ] = { "FlowRate" , extension };
  filenames[ node_injection ] = { "NodeInjection" , extension };

 }

/*--------------------------------------------------------------------------*/

 void print_flow( const UCBlock * uc_block ) const {

  const auto & blocks = uc_block->get_network_blocks();

  std::ofstream output( filenames[ flow ].name() , open_mode() );

  auto get_power_flow =
   []( NetworkBlock * block , Index line ) -> double {
    if( auto dc = dynamic_cast< DCNetworkBlock * >( block ) ) {
     const auto & power_flow = dc->get_power_flow();
     if( line < power_flow.size() )
      return( power_flow[ line ].get_value() );
    }
    return( 0 );
   };

  std::function< std::string( Index ) > get_line_name = []( Index line ) {
   return( "Line_" + std::to_string( line ) );
  };

  if( auto network_data = dynamic_cast< DCNetworkBlock::DCNetworkData * >(
   uc_block->get_NetworkData() ) ) {
   const auto & line_names = network_data->get_line_names();
   if( ! line_names.empty() )
    get_line_name = [ &line_names ]( Index line ) {
     assert( line < line_names.size() );
     return( line_names[ line ] );
    };
  }

  print_line_data( output , blocks , get_power_flow , get_line_name );

  output.close();
 }

/*--------------------------------------------------------------------------*/

 void print_node_injection( const UCBlock * uc_block ) const {

  std::ofstream output( filenames[ node_injection ].name() , open_mode() );

  auto get_node_injection =
   []( NetworkBlock * block , Index node ) -> double {
    if( auto dc = dynamic_cast< DCNetworkBlock * >( block ) ) {
     const auto & node_injection = dc->get_node_injection();
     if( node < dc->get_number_nodes() )
      return( node_injection[ node ].get_value() );
    }
    return( 0 );
   };

  print_node_data( output , uc_block , get_node_injection );

  output.close();
 }

/*--------------------------------------------------------------------------*/

 void print_node_injection_duals( UCBlock * uc_block ) const {

  std::ofstream output( filenames[ marginal_cost_active_power_demand ].name() ,
                        open_mode() );

  auto get_node_injection_dual =
   []( UCBlock * block , Index time , Index node ) -> double {
    const auto & constraints = block->get_node_injection_constraints();
    if( time < constraints.size() && node < constraints[ time ].size() )
     return( - constraints[ time ][ node ].get_dual() );
    return( 0 );
   };

  print_data( output , uc_block , get_node_injection_dual ,
              get_number_nodes( uc_block ) );

  output.close();
 }

/*--------------------------------------------------------------------------*/

 /// dual values for the primary demand constraints
 void print_primary_demand_duals( UCBlock * uc_block ) const {

  std::ofstream output( filenames[ marginal_cost_primary ].name() ,
                        open_mode() );

  auto get_primary_demand_dual =
   []( UCBlock * block , Index time , Index zone ) -> double {
    const auto & constraints = block->get_primary_demand_constraints();
    if( time < constraints.size() && zone < constraints[ time ].size() )
     return( constraints[ time ][ zone ].get_dual() );
    return( 0 );
   };

  print_data( output , uc_block , get_primary_demand_dual ,
              uc_block->get_number_primary_zones() ,
              []( Index i ) { return( "Zone_" + std::to_string( i ) ); } );

  output.close();
 }

/*--------------------------------------------------------------------------*/

 /// dual values for the secondary demand constraints
 void print_secondary_demand_duals( UCBlock * uc_block ) const {
  std::ofstream output( filenames[ marginal_cost_secondary ].name() ,
                        open_mode() );

  auto get_secondary_demand_dual =
   []( UCBlock * block , Index time , Index zone ) -> double {
    const auto & constraints = block->get_secondary_demand_constraints();
    if( time < constraints.size() && zone < constraints[ time ].size() )
     return( constraints[ time ][ zone ].get_dual() );
    return( 0 );
   };

  print_data( output , uc_block , get_secondary_demand_dual ,
              uc_block->get_number_secondary_zones() ,
              []( Index i ) { return( "Zone_" + std::to_string( i ) ); } );

  output.close();
 }

/*--------------------------------------------------------------------------*/

 /// dual values for the inertia demand constraints
 void print_inertia_demand_duals( UCBlock * uc_block ) const {
  std::ofstream output( filenames[ marginal_cost_inertia ].name() ,
                        open_mode() );

  auto get_inertia_demand_dual =
   []( UCBlock * block , Index time , Index zone ) -> double {
    const auto & constraints = block->get_inertia_demand_constraints();
    if( time < constraints.size() && zone < constraints[ time ].size() )
     return( constraints[ time ][ zone ].get_dual() );
    return( 0 );
   };

  print_data( output , uc_block , get_inertia_demand_dual ,
              uc_block->get_number_inertia_zones() ,
              []( Index i ) { return( "Zone_" + std::to_string( i ) ); } );

  output.close();
 }

/*--------------------------------------------------------------------------*/

 /// dual values for the maximum pollutant emission constraints
 void print_maximum_pollutant_emission_duals( UCBlock * uc_block ) const {

  const auto number_pollutants = uc_block->get_number_pollutants();

  for( Index p = 0 ; p < number_pollutants ; ++p ) {

   std::ofstream output( get_marginal_pollutant_filename( p ) , open_mode() );

   // Header

   for( Index z = 0 ; z < uc_block->get_number_pollutant_zones()[ p ] ; ++z )
    output << separator_character << "Zone_" << 0;
   output << std::endl;

   // Values
   for( Index z = 0 ; z < uc_block->get_number_pollutant_zones()[ p ] ; ++z ) {
    if( z > 0 ) output << separator_character;
    output << uc_block->get_pollutant_constraints()[ p ][ z ].get_dual();
   }

   output.close();
  }
 }

/*--------------------------------------------------------------------------*/

 /// dual values for the power flow limit constraints
 void print_power_flow_limit_duals( UCBlock * uc_block ) const {

  std::ofstream output( filenames[ marginal_cost_flows ].name() , open_mode() );

  auto get_power_flow_limit_dual =
   []( NetworkBlock * block , Index line ) -> double {
    if( auto dc = dynamic_cast< DCNetworkBlock * >( block ) ) {
     if( auto network_data = dc->get_NetworkData() ) {
      if( static_cast< DCNetworkBlock::DCNetworkData * >(network_data)
           ->get_lines_type() == DCNetworkBlock::kHVDC ) {
       const auto & constraints = dc->get_power_flow_limit_HVDC_bounds();
       if( line < constraints.size() )
        return( constraints[ line ].get_dual() );
      } else {
       const auto & constraints = dc->get_power_flow_limit_constraints();
       if( line < constraints.size() )
        return( constraints[ line ].get_dual() );
      }
     }
    }
    return( 0 );
   };

  std::function< std::string( Index ) > get_line_name = []( Index line ) {
   return( "Line_" + std::to_string( line ) );
  };

  if( auto network_data = dynamic_cast< DCNetworkBlock::DCNetworkData * >(
   uc_block->get_NetworkData() ) ) {
   const auto & line_names = network_data->get_line_names();
   if( ! line_names.empty() )
    get_line_name = [ &line_names ]( Index line ) {
     assert( line < line_names.size() );
     return( line_names[ line ] );
    };
  }

  print_line_data( output , uc_block->get_network_blocks() ,
                   get_power_flow_limit_dual , get_line_name );

  output.close();
 }

/*--------------------------------------------------------------------------*/

 void print_duals( UCBlock * block ) const {
  print_power_flow_limit_duals( block );
  print_node_injection_duals( block );
  print_primary_demand_duals( block );
  print_secondary_demand_duals( block );
  print_inertia_demand_duals( block );
  print_maximum_pollutant_emission_duals( block );
 }

/*--------------------------------------------------------------------------*/

 void print_demand( UCBlock * block ) const {

  std::ofstream output( filenames[ demand ].name() , open_mode() );

  auto get_demand =
   []( UCBlock * block , Index time , Index node ) {
    const auto & network_blocks = block->get_network_blocks();
    if( ! network_blocks.empty() ) {
     assert( network_blocks.size() > time );
     auto network_block = block->get_network_blocks()[ time ];
     return( network_block->get_active_demand()[ node ] );
    } else {
     return( block->get_node_injection_constraints()
     [ time ][ node ].get_rhs() );
    }
   };

  print_data( output , block , get_demand , get_number_nodes( block ) );

  output.close();
 }

/*--------------------------------------------------------------------------*/

 void print_active_power( const std::vector< UnitBlock * > & blocks ) const {

  std::ofstream output( filenames[ active_power ].name() , open_mode() );

  auto get_active_power =
   []( UnitBlock * block , Index g , Index t ) -> double {
    if( const auto active_power = block->get_active_power( g ) )
     return( active_power + t )->get_value();
    return( 0 );
   };

  print_generator_data( output , blocks , get_active_power );

  output.close();
 }

/*--------------------------------------------------------------------------*/

 void print_max_power( const std::vector< UnitBlock * > & blocks ) const {

  std::ofstream output( filenames[ max_power ].name() , open_mode() );

  auto get_max_power = []( UnitBlock * block , Index g , Index t ) {
   return( block->get_max_power( t , g ) );
  };

  print_generator_data( output , blocks , get_max_power );

  output.close();
 }

/*--------------------------------------------------------------------------*/

 void print_primary_spinning_reserve(
  const std::vector< UnitBlock * > & blocks ) const {

  std::ofstream output( filenames[ primary_spinning_reserve ].name() ,
                        open_mode() );

  auto get_primary_spinning_reserve =
   []( UnitBlock * block , Index g , Index t ) -> double {
    if( auto reserve = block->get_primary_spinning_reserve( g ) )
     return( reserve + t )->get_value();
    return( 0 );
   };

  print_generator_data( output , blocks , get_primary_spinning_reserve );

  output.close();
 }

/*--------------------------------------------------------------------------*/

 void print_secondary_spinning_reserve(
  const std::vector< UnitBlock * > & blocks ) const {

  std::ofstream output( filenames[ secondary_spinning_reserve ].name() ,
                        open_mode() );

  auto get_secondary_spinning_reserve =
   []( UnitBlock * block , Index g , Index t ) -> double {
    if( auto reserve = block->get_secondary_spinning_reserve( g ) )
     return( reserve + t )->get_value();
    return( 0 );
   };

  print_generator_data( output , blocks , get_secondary_spinning_reserve );

  output.close();
 }

/*--------------------------------------------------------------------------*/

 void print_volume( const std::vector< HydroUnitBlock * > & blocks ) const {

  std::ofstream output( filenames[ volume ].name() , open_mode() );

  auto get_volume =
   []( HydroUnitBlock * block , Index r , Index t ) -> double {
    if( const auto volume = block->get_volume( r , t ) )
     return( volume->get_value() );
    return( 0 );
   };

  print_reservoir_data( output , blocks , get_volume );

  output.close();
 }

/*--------------------------------------------------------------------------*/

 void print_inflows( const std::vector< HydroUnitBlock * > & blocks ) const {

  std::ofstream output( filenames[ inflow ].name() , open_mode() );

  auto get_inflow =
   []( HydroUnitBlock * block , Index r , Index t ) -> double {
    const auto & inflows = block->get_inflows();
    if( r < inflows.size() && t < inflows[ r ].size() )
     return( inflows[ r ][ t ] );
    return( 0 );
   };

  print_reservoir_data( output , blocks , get_inflow );

  output.close();
 }

/*--------------------------------------------------------------------------*/

 void print_flow_rate( const std::vector< HydroUnitBlock * > & blocks ) const {

  std::ofstream output( filenames[ flow_rate ].name() , open_mode() );

  auto get_flow_rate =
   []( UnitBlock * block , Index g , Index t ) -> double {
    if( auto flow_rate = static_cast< HydroUnitBlock * >
    ( block )->get_flow_rate( g , t ) )
     return( flow_rate->get_value() );
    return( 0 );
   };

  std::vector< UnitBlock * > unit_blocks;
  unit_blocks.reserve( blocks.size() );
  for( auto block : blocks )
   unit_blocks.push_back( block );

  print_generator_data( output , unit_blocks , get_flow_rate );

  output.close();
 }

/*--------------------------------------------------------------------------*/

 void print_storage( const std::vector< UnitBlock * > & blocks ) const {

  std::ofstream output( filenames[ volume ].name() , open_mode() );

  auto get_storage =
   []( UnitBlock * block , Index r , Index t ) -> double {
    if( auto hydro = dynamic_cast< HydroUnitBlock * >( block ) ) {
     if( auto volume = hydro->get_volume( r , t ) )
      return( volume->get_value() );
     return( 0 );
    } else if( auto battery = dynamic_cast< BatteryUnitBlock * >( block ) ) {
     const auto & storage = battery->get_storage_level();
     if( t < storage.size() )
      return( storage[ t ].get_value() );
     return( 0 );
    } else
     throw( std::invalid_argument(
      "UCBlockSolutionOutput::print_storage: invalid type of UnitBlock: " +
      block->classname() ) );
   };

  print_storage_data( output , blocks , get_storage );

  output.close();
 }

/*--------------------------------------------------------------------------*/

 void print( Block * block ) const {
  if( auto uc_block = dynamic_cast< UCBlock * >( block ) ) {
   auto unit_blocks = get_unit_blocks( uc_block );
   print_active_power( unit_blocks );
   print_primary_spinning_reserve( unit_blocks );
   print_secondary_spinning_reserve( unit_blocks );
   print_storage( get_unit_blocks_with_storage( uc_block ) );
   print_flow( uc_block );
   print_duals( uc_block );
   print_demand( uc_block );
   print_max_power( unit_blocks );
  }
 }

/*--------------------------------------------------------------------------*/

 void set_separator_character( char separator_character ) {
  this->separator_character = separator_character;
 }

/*--------------------------------------------------------------------------*/

 void set_filenames_suffix( const std::string & suffix ) {
  for( auto & filename : filenames )
   filename.suffix = suffix;
 }

/*--------------------------------------------------------------------------*/

 void set_append( bool append = true ) {
  this->append = append;
 }

/*--------------------------------------------------------------------------*/

 void set_initial_time( Index initial_time = 0 ) {
  this->initial_time = initial_time;
 }

/*--------------------------------------------------------------------------*/

 void copy( const std::string & current_suffix , const std::string & suffix ,
            const UCBlock * uc_block ) {
  const auto copy_options = std::filesystem::copy_options::overwrite_existing;
  for( auto & filename : filenames ) {
   filename.suffix = current_suffix;
   if( filename.prefix == filenames[ marginal_pollutant ].prefix ) {
    const auto number_pollutants = uc_block->get_number_pollutants();
    for( Index p = 0 ; p < number_pollutants ; ++p ) {
     const auto filename = get_marginal_pollutant_filename( p );
     if( std::filesystem::is_regular_file( filename ) ) {
      const auto new_filename = filename + suffix;
      std::filesystem::copy( filename , new_filename , copy_options );
     }
    }
   }
   else {
    auto new_filename = filename;
    new_filename.suffix += suffix;
    if( std::filesystem::is_regular_file( filename.name() ) )
     std::filesystem::copy( filename.name() , new_filename.name() ,
                            copy_options );
   }
  }
 }

/*--------------------------------------------------------------------------*/

 void rename( const std::string & suffix_to_keep ,
              const std::string & suffix_to_remove ,
              const UCBlock * uc_block ) {
  for( auto & filename : filenames ) {
   filename.suffix = suffix_to_keep;
   if( filename.prefix == filenames[ marginal_pollutant ].prefix ) {
    const auto number_pollutants = uc_block->get_number_pollutants();
    for( Index p = 0 ; p < number_pollutants ; ++p ) {
     const auto new_filename = get_marginal_pollutant_filename( p );
     const auto old_filename = new_filename + suffix_to_remove;
     if( std::filesystem::is_regular_file( old_filename ) )
      std::filesystem::rename( old_filename , new_filename );
    }
   }
   else {
    const auto new_filename = filename.name();
    const auto old_filename = new_filename + suffix_to_remove;
    if( std::filesystem::is_regular_file( old_filename ) )
     std::filesystem::rename( old_filename , new_filename );
   }
  }
 }

/*--------------------------------------------------------------------------*/
/*--------------------- PRIVATE PART OF THE CLASS --------------------------*/
/*--------------------------------------------------------------------------*/

 private:

/*--------------------------------------------------------------------------*/
/*--------------------------- PRIVATE METHODS ------------------------------*/
/*--------------------------------------------------------------------------*/

 std::string get_name( const Block * block ) const {
  const auto name = block->name();
  if( ! name.empty() )
   return( name );
  return( block->classname() );
 }

/*--------------------------------------------------------------------------*/

 Index get_number_nodes( const UCBlock * block ) const {
  if( ! block ) return( 0 );
  auto network_data = block->get_NetworkData();
  return( network_data ? network_data->get_number_nodes() : 1 );
 }

/*--------------------------------------------------------------------------*/

 Index get_number_lines( const NetworkBlock * block ) const {
  if( ! block ) return( 0 );
  auto network_data = static_cast< DCNetworkBlock::DCNetworkData * >(
   block->get_NetworkData() );
  return( network_data ? network_data->get_number_lines() : 0 );
 }

/*--------------------------------------------------------------------------*/

 Index get_number_nodes( const NetworkBlock * block ) const {
  if( ! block ) return( 0 );
  auto network_data = block->get_NetworkData();
  return( network_data ? network_data->get_number_nodes() : 1 );
 }

/*--------------------------------------------------------------------------*/

 std::string get_marginal_pollutant_filename( Index pollutant ) const {
  return( filenames[ marginal_pollutant ].prefix +
         std::to_string( pollutant ) + filenames[ marginal_pollutant ].suffix );
 }

/*--------------------------------------------------------------------------*/

 template< class F , class G >
 void print_line_data( std::ostream & output ,
                       const std::vector< NetworkBlock * > & blocks ,
                       const F & get_data , const G & get_line_name ,
                       const int precision = 20 ) const {
  if( blocks.empty() ) return;

  auto number_lines = get_number_lines( blocks.front() );

  // Header

  if( ! append ) {
   output << "Timestep";
   for( Index line = 0 ; line < number_lines ; ++line )
    output << separator_character << get_line_name( line );
   output << std::endl;
  }

  // Values

  Index t = 0;
  if( append )
   t = initial_time;

  for( auto block : blocks ) {
   output << t;
   for( Index line = 0 ; line < number_lines ; ++line )
    output << separator_character << std::setprecision( precision )
           << get_data( block , line );
   output << std::endl;
   ++t;
  }
 }

/*--------------------------------------------------------------------------*/

 template< class F >
 void print_node_data( std::ostream & output , const UCBlock * uc_block ,
                       const F & get_data , const int precision = 20 ) const {


  const auto & blocks = uc_block->get_network_blocks();

  if( blocks.empty() ) return;

  auto number_nodes = get_number_nodes( blocks.front() );

  // Header

  std::function< std::string( Index ) > get_node_name = []( Index node ) {
   return( "Node_" + std::to_string( node ) );
  };

  if( auto network_data = dynamic_cast< DCNetworkBlock::DCNetworkData * >(
   uc_block->get_NetworkData() ) ) {
   const auto & node_names = network_data->get_node_names();
   if( ! node_names.empty() )
    get_node_name = [ &node_names ]( Index node ) {
     assert( node < node_names.size() );
     return( node_names[ node ] );
    };
  }

  if( ! append ) {
   output << "Timestep";
   for( Index node = 0 ; node < number_nodes ; ++node )
    output << separator_character << get_node_name( node ) << node;
   output << std::endl;
  }

  // Values

  Index t = 0;
  if( append )
   t = initial_time;

  for( auto block : blocks ) {
   output << t;
   for( Index line = 0 ; line < number_nodes ; ++line )
    output << separator_character << std::setprecision( precision )
           << get_data( block , line );
   output << std::endl;
   ++t;
  }
 }

/*--------------------------------------------------------------------------*/

 template< class F , class G >
 void print_data( std::ostream & output , UCBlock * block , const F & get_data ,
                  const Index columns , const G & get_column_name ,
                  const std::string first_column_header ,
                  const Index rows , const Index initial_row = 0 ,
                  const int precision = 20 ) const {
  // Header

  if( ! append ) {
   output << first_column_header;
   for( Index i = 0 ; i < columns ; ++i )
    output << separator_character << get_column_name( i );
   output << std::endl;
  }

  // Values

  for( Index r = 0 ; r < rows ; ++r ) {
   output << ( r + initial_row );
   for( Index i = 0 ; i < columns ; ++i )
    output << separator_character << std::setprecision( precision )
           << get_data( block , r , i );
   output << std::endl;
  }
 }

/*--------------------------------------------------------------------------*/

 template< class F >
 void print_data( std::ostream & output , UCBlock * block , const F & get_data ,
                  const Index columns , const int precision = 20 ) const {
  std::function< std::string( Index ) > get_node_name = []( Index node ) {
   return( "Node_" + std::to_string( node ) );
  };

  if( auto network_data = dynamic_cast< DCNetworkBlock::DCNetworkData * >(
   block->get_NetworkData() ) ) {
   const auto & node_names = network_data->get_node_names();
   if( ! node_names.empty() )
    get_node_name = [ &node_names ]( Index node ) {
     assert( node < node_names.size() );
     return( node_names[ node ] );
    };
  }

  print_data( output , block , get_data , columns , get_node_name ,
              "Timestep" , block->get_time_horizon() , initial_time ,
              precision );
 }

/*--------------------------------------------------------------------------*/

 template< class F, class G >
 void print_data( std::ostream & output , UCBlock * block , const F & get_data ,
                  const Index columns , const G & get_column_name ,
                  const int precision = 20 ) const {
  print_data( output , block , get_data , columns , get_column_name ,
              "Timestep" , block->get_time_horizon() , initial_time ,
              precision );
 }

/*--------------------------------------------------------------------------*/

 std::vector< UnitBlock * > get_unit_blocks( UCBlock * uc_block ) const {

  std::vector< UnitBlock * > unit_blocks;
  unit_blocks.reserve( uc_block->get_number_units() );

  for( Index i = 0 ; i < uc_block->get_number_units() ; ++i ) {

   auto block = uc_block->get_unit_block( i );
   if( auto hydro_system = dynamic_cast< HydroSystemUnitBlock * >( block ) )
    for( Index h = 0 ; h < hydro_system->get_number_hydro_units() ; ++h )
     unit_blocks.push_back( hydro_system->get_hydro_unit_block( h ) );
   else
    unit_blocks.push_back( block );
  }

  return( unit_blocks );
 }

/*--------------------------------------------------------------------------*/

 std::vector< HydroUnitBlock * > get_hydro_unit_blocks(
  UCBlock * uc_block ) const {

  std::vector< HydroUnitBlock * > hydro_unit_blocks;

  for( Index i = 0 ; i < uc_block->get_number_units() ; ++i ) {
   auto block = uc_block->get_unit_block( i );
   if( auto hydro_system = dynamic_cast< HydroSystemUnitBlock * >( block ) )
    for( Index h = 0 ; h < hydro_system->get_number_hydro_units() ; ++h )
     hydro_unit_blocks.push_back( hydro_system->get_hydro_unit_block( h ) );
   else if( auto hydro = dynamic_cast< HydroUnitBlock * >( block ) )
    hydro_unit_blocks.push_back( hydro );
  }

  return( hydro_unit_blocks );
 }

/*--------------------------------------------------------------------------*/

 std::vector< UnitBlock * > get_unit_blocks_with_storage(
  UCBlock * uc_block ) const {

  std::vector< UnitBlock * > unit_blocks;

  for( Index i = 0 ; i < uc_block->get_number_units() ; ++i ) {
   auto block = uc_block->get_unit_block( i );
   if( auto battery = dynamic_cast< BatteryUnitBlock * >( block ) )
    unit_blocks.push_back( battery );
   else if( auto hydro_system = dynamic_cast< HydroSystemUnitBlock * >( block ) )
    for( Index h = 0 ; h < hydro_system->get_number_hydro_units() ; ++h )
     unit_blocks.push_back( hydro_system->get_hydro_unit_block( h ) );
   else if( auto hydro = dynamic_cast< HydroUnitBlock * >( block ) )
    unit_blocks.push_back( hydro );
  }

  return( unit_blocks );
 }

/*--------------------------------------------------------------------------*/

 template< class F >
 void print_generator_data( std::ostream & output ,
                            const std::vector< UnitBlock * > & blocks ,
                            const F & get_data ,
                            const int precision = 20 ) const {

  if( blocks.empty() ) return;

  // Header

  if( ! append ) {

   output << "Timestep";
   for( auto block : blocks ) {
    auto block_name = get_name( block );
    const auto number_generators = block->get_number_generators();
    if( number_generators <= 1 )
     output << separator_character << block_name;
    else
     for( Index g = 0 ; g < number_generators ; ++g )
      output << separator_character << block_name << "_" << g;
   }
   output << std::endl;
  }

  // Values

  auto time_horizon = blocks.front()->get_time_horizon();

  for( Index t = 0 ; t < time_horizon ; ++t ) {

   Index time = t;
   if( append )
    time = initial_time + t;

   output << time;
   for( auto block : blocks ) {
    const auto number_generators = block->get_number_generators();
    for( Index g = 0 ; g < number_generators ; ++g )
     output << separator_character << std::setprecision( precision )
            << get_data( block , g , t );
   }
   output << std::endl;
  }
 }

/*--------------------------------------------------------------------------*/

 template< class F >
 void print_reservoir_data( std::ostream & output ,
                            const std::vector< HydroUnitBlock * > & blocks ,
                            const F & get_data ,
                            const int precision = 20 ) const {

  if( blocks.empty() ) return;

  // Header

  if( ! append ) {
   output << "Timestep";
   for( auto block : blocks ) {
    auto block_name = get_name( block );
    const auto number_reservoirs = block->get_number_reservoirs();
    if( number_reservoirs <= 1 )
     output << separator_character << block_name;
    else
     for( Index r = 0 ; r < number_reservoirs ; ++r )
      output << separator_character << block_name << "_" << r;
   }
   output << std::endl;
  }

  // Values

  auto time_horizon = blocks.front()->get_time_horizon();

  for( Index t = 0 ; t < time_horizon ; ++t ) {

   Index time = t;
   if( append )
    time = initial_time + t;

   output << time;
   for( auto block : blocks ) {
    const auto number_reservoirs = block->get_number_reservoirs();
    for( Index r = 0 ; r < number_reservoirs ; ++r )
     output << separator_character << std::setprecision( precision )
            << get_data( block , r , t );
   }
   output << std::endl;
  }
 }

/*--------------------------------------------------------------------------*/

 template< class F >
 void print_storage_data( std::ostream & output ,
                          const std::vector< UnitBlock * > & blocks ,
                          const F & get_data ,
                          const int precision = 20 ) const {

  if( blocks.empty() ) return;

  // Header

  if( ! append ) {
   output << "Timestep";
   for( auto block : blocks ) {
    auto block_name = get_name( block );
    if( auto hydro = dynamic_cast< HydroUnitBlock * >( block ) ) {
     const auto number_reservoirs = hydro->get_number_reservoirs();
     if( number_reservoirs <= 1 )
      output << separator_character << block_name;
     else
      for( Index r = 0 ; r < number_reservoirs ; ++r )
       output << separator_character << block_name << "_" << r;
    } else if( auto battery = dynamic_cast< BatteryUnitBlock * >( block ) )
     output << separator_character << block_name;
    else
     throw( std::invalid_argument(
      "UCBlockSolutionOutput::print_storage_data: invalid type of UnitBlock: "
      + block->classname() ) );
   }
   output << std::endl;
  }

  // Values

  auto time_horizon = blocks.front()->get_time_horizon();

  for( Index t = 0 ; t < time_horizon ; ++t ) {

   Index time = t;
   if( append )
    time = initial_time + t;

   output << time;
   for( auto block : blocks ) {

    if( auto hydro = dynamic_cast< HydroUnitBlock * >( block ) ) {
     const auto number_reservoirs = hydro->get_number_reservoirs();
     for( Index r = 0 ; r < number_reservoirs ; ++r )
      output << separator_character << std::setprecision( precision )
             << get_data( hydro , r , t );
    } else if( auto battery = dynamic_cast< BatteryUnitBlock * >( block ) )
     output << separator_character << std::setprecision( precision )
            << get_data( battery , t , t );
    else
     throw( std::invalid_argument(
      "UCBlockSolutionOutput::print_storage_data: invalid type of UnitBlock: "
      + block->classname() ) );
   }
   output << std::endl;
  }
 }

/*--------------------------------------------------------------------------*/

 std::ios_base::openmode open_mode() const {
  if( append )
   return( std::ios::out | std::ios::app );
  return( std::ios::out );
 }

/*--------------------------------------------------------------------------*/
/*---------------------------- PRIVATE TYPES  ------------------------------*/
/*--------------------------------------------------------------------------*/

 struct Filename
 {
  std::string prefix;
  std::string suffix;

  std::string name() const { return( prefix + suffix ); };
 };

 enum files
 {
  active_power = 0 ,
  primary_spinning_reserve ,
  secondary_spinning_reserve ,
  volume ,
  flow ,
  marginal_cost_active_power_demand ,
  marginal_cost_primary ,
  marginal_cost_secondary ,
  marginal_cost_inertia ,
  marginal_cost_flows ,
  marginal_pollutant ,
  demand ,
  max_power ,
  inflow ,
  flow_rate ,
  node_injection ,
  number_of_files
 };

/*--------------------------------------------------------------------------*/
/*---------------------------- PRIVATE FIELDS  -----------------------------*/
/*--------------------------------------------------------------------------*/

 char separator_character = ',';
 bool append = false;
 Index initial_time = 0;
 std::vector< Filename > filenames;

/*--------------------------------------------------------------------------*/
/*--------------------------------------------------------------------------*/

};  // end( class UCBlockSolutionOutput )

/*--------------------------------------------------------------------------*/
/*--------------------------------------------------------------------------*/

#endif  /* UCBlockSolutionOutput.h included */

/*--------------------------------------------------------------------------*/
/*-------------------- End File UCBlockSolutionOutput.h --------------------*/
/*--------------------------------------------------------------------------*/
